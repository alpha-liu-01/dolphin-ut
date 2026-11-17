// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/UserData.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>

#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/ScopeGuard.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "DolphinUT/Emulator.h"
#include "DolphinUT/PortDefaults.h"

namespace fs = std::filesystem;

namespace
{
// The user folder, without the trailing "/" Dolphin keeps (it would be an empty last element, which
// lexically_relative does not match).
fs::path UserFolder()
{
  fs::path folder = fs::path(File::GetUserPath(D_USER_IDX)).lexically_normal();
  if (!folder.has_filename())
    folder = folder.parent_path();
  return folder;
}

// path is root or under it (both absolute), without leaving it through "..".
bool IsUnder(const fs::path& path, const fs::path& root)
{
  const fs::path relative = path.lexically_normal().lexically_relative(root.lexically_normal());
  if (relative.empty())
    return false;
  for (const auto& part : relative)
  {
    if (part == "..")
      return false;
  }
  return true;
}
}  // namespace

UserData::UserData(QObject* parent) : QObject(parent)
{
}

UserData::~UserData()
{
  m_cancel = true;
  if (m_thread.joinable())
    m_thread.join();
}

QString UserData::GetBackupFolder() const
{
  return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
         QStringLiteral("/Dolphin/Backups");
}

QString UserData::newBackupPath() const
{
  return GetBackupFolder() + QStringLiteral("/dolphin-emu-%1.zip")
                                 .arg(QDateTime::currentDateTime().toString(
                                     QStringLiteral("yyyy-MM-dd-HHmm")));
}

bool UserData::isInUserFolder(const QString& path) const
{
  return IsUnder(fs::path(path.toStdString()), UserFolder());
}

bool UserData::Run(const QString& task, std::function<Result()> work)
{
  if (m_busy || !Core::IsUninitialized(Core::System::GetInstance()))
    return false;
  if (m_thread.joinable())
    m_thread.join();
  m_busy = true;
  m_task = task;
  m_cancel = false;
  m_progress = 0;
  m_progress_total = 0;
  m_progress_text.clear();
  emit busyChanged();
  emit progressChanged();
  Emulator::LogFromAnyThread(QStringLiteral("user data %1: started").arg(task));
  m_thread = std::thread([this, task, work = std::move(work)] {
    const Result result = work();
    const bool cancelled = m_cancel.load();
    QMetaObject::invokeMethod(this, [this, task, result, cancelled] {
      Emulator::LogFromAnyThread(QStringLiteral("user data %1: %2 (%3)")
                                     .arg(task,
                                          cancelled  ? QStringLiteral("cancelled") :
                                          result.ok ? QStringLiteral("done") :
                                                      QStringLiteral("failed"),
                                          result.message));
      m_busy = false;
      emit busyChanged();
      emit finished(task, result.ok && !cancelled, cancelled, result.message);
    });
  });
  return true;
}

void UserData::SetProgress(int done, int total, const QString& text)
{
  QMetaObject::invokeMethod(this, [this, done, total, text] {
    m_progress = done;
    m_progress_total = total;
    m_progress_text = text;
    emit progressChanged();
  });
}

bool UserData::exportTo(const QString& zip_path)
{
  // Not into the folder being exported.
  if (zip_path.isEmpty() || isInUserFolder(zip_path))
    return false;
  // The settings as they are now (changes are saved as they are made; this is a safeguard).
  Config::Save();
  return Run(QStringLiteral("export"), [this, zip_path] { return Export(zip_path); });
}

// UserDataActivity.exportUserData: every file under the user folder by its relative path, and
// empty folders as folder entries. The file times are kept (minizip-ng stores them).
UserData::Result UserData::Export(const QString& zip_path)
{
  const Result failure{false, QStringLiteral("Failed to export user data.")};
  const fs::path root = UserFolder();
  std::vector<fs::path> files;
  std::vector<fs::path> empty_folders;
  std::error_code error;
  for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied,
                                                  error);
       !error && it != fs::recursive_directory_iterator(); it.increment(error))
  {
    if (it->is_symlink(error))
    {
      if (it->is_directory(error))
        it.disable_recursion_pending();
      continue;
    }
    if (it->is_directory(error))
    {
      if (fs::is_empty(it->path(), error))
        empty_folders.push_back(it->path());
    }
    else if (it->is_regular_file(error))
    {
      files.push_back(it->path());
    }
  }
  if (error)
  {
    Emulator::LogFromAnyThread(QStringLiteral("user data export: listing failed: %1")
                                   .arg(QString::fromStdString(error.message())));
    return failure;
  }

  const std::string zip = zip_path.toStdString();
  fs::create_directories(fs::path(zip).parent_path(), error);
  void* writer = mz_zip_writer_create();
  if (!writer)
    return failure;
  bool open = false;
  Common::ScopeGuard guard{[&] {
    if (open)
      mz_zip_writer_close(writer);
    mz_zip_writer_delete(&writer);
  }};
  mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
  mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_DEFAULT);
  if (mz_zip_writer_open_file(writer, zip.c_str(), 0, 0) != MZ_OK)
    return failure;
  open = true;

  const int total = static_cast<int>(files.size());
  for (int i = 0; i < total; ++i)
  {
    if (m_cancel)
      break;
    const std::string name = files[i].lexically_relative(root).generic_string();
    SetProgress(i, total, QString::fromStdString(name));
    if (mz_zip_writer_add_file(writer, files[i].c_str(), name.c_str()) != MZ_OK)
    {
      // A file that went away since the listing (a cache or log being replaced) is left out.
      if (!fs::exists(files[i], error))
      {
        Emulator::LogFromAnyThread(
            QStringLiteral("user data export: %1 went away, left out").arg(QString::fromStdString(name)));
        continue;
      }
      Emulator::LogFromAnyThread(
          QStringLiteral("user data export: could not add %1").arg(QString::fromStdString(name)));
      m_cancel = false;
      mz_zip_writer_close(writer);
      open = false;
      fs::remove(zip, error);
      return failure;
    }
  }
  for (const fs::path& folder : empty_folders)
  {
    if (m_cancel)
      break;
    mz_zip_file info = {};
    const std::string name = folder.lexically_relative(root).generic_string() + "/";
    info.filename = name.c_str();
    info.modified_date = std::time(nullptr);
    info.compression_method = MZ_COMPRESS_METHOD_STORE;
    info.external_fa = 0x10;  // a folder (MS-DOS attribute)
    if (mz_zip_writer_entry_open(writer, &info) == MZ_OK)
      mz_zip_writer_entry_close(writer);
  }
  const bool closed = mz_zip_writer_close(writer) == MZ_OK;
  open = false;
  if (m_cancel || !closed)
  {
    fs::remove(zip, error);
    return m_cancel ? Result{false, QString()} : failure;
  }
  SetProgress(total, total, QString());
  return {true, QStringLiteral("Your user data has been exported.")};
}

QString UserData::checkBackup(const QString& zip_path) const
{
  if (isInUserFolder(zip_path))
    return QStringLiteral("Choose a file outside the user data: importing replaces everything in it.");
  void* reader = mz_zip_reader_create();
  if (!reader)
    return QStringLiteral("Failed to import user data.");
  Common::ScopeGuard guard{[&] { mz_zip_reader_delete(&reader); }};
  if (mz_zip_reader_open_file(reader, zip_path.toStdString().c_str()) != MZ_OK)
    return QStringLiteral("This file doesn't seem to contain Dolphin user data.");
  const bool found = mz_zip_reader_locate_entry(reader, "Config/Dolphin.ini", 0) == MZ_OK;
  mz_zip_reader_close(reader);
  return found ? QString() : QStringLiteral("This file doesn't seem to contain Dolphin user data.");
}

bool UserData::importFrom(const QString& zip_path)
{
  if (!checkBackup(zip_path).isEmpty())
    return false;
  return Run(QStringLiteral("import"), [this, zip_path] { return Import(zip_path); });
}

// UserDataActivity.importUserData: the user folder emptied, then every entry extracted to it; an
// entry that would land outside it ends the import ("Zip file attempted path traversal!"). The game
// list cache goes, as on Android. Not cancellable once the folder is emptied.
UserData::Result UserData::Import(const QString& zip_path)
{
  const Result failure{false, QStringLiteral("Failed to import user data.")};
  const fs::path root = UserFolder();
  void* reader = mz_zip_reader_create();
  if (!reader)
    return failure;
  Common::ScopeGuard guard{[&] { mz_zip_reader_delete(&reader); }};
  if (mz_zip_reader_open_file(reader, zip_path.toStdString().c_str()) != MZ_OK)
    return failure;

  // Every entry checked before anything is deleted.
  std::vector<std::string> names;
  if (mz_zip_reader_goto_first_entry(reader) == MZ_OK)
  {
    do
    {
      mz_zip_file* info = nullptr;
      if (mz_zip_reader_entry_get_info(reader, &info) != MZ_OK || !info->filename)
        return failure;
      const std::string name = info->filename;
      if (fs::path(name).is_absolute() || !IsUnder(root / name, root))
      {
        Emulator::LogFromAnyThread(QStringLiteral("user data import: path traversal in %1")
                                       .arg(QString::fromStdString(name)));
        return failure;
      }
      names.push_back(name);
    } while (mz_zip_reader_goto_next_entry(reader) == MZ_OK);
  }

  std::error_code error;
  for (const auto& child : fs::directory_iterator(root, error))
  {
    fs::remove_all(child.path(), error);
    if (error)
    {
      Emulator::LogFromAnyThread(QStringLiteral("user data import: could not delete %1: %2")
                                     .arg(QString::fromStdString(child.path().string()),
                                          QString::fromStdString(error.message())));
      return failure;
    }
  }

  const int total = static_cast<int>(names.size());
  int done = 0;
  if (mz_zip_reader_goto_first_entry(reader) == MZ_OK)
  {
    do
    {
      const std::string& name = names[done];
      SetProgress(done, total, QString::fromStdString(name));
      const fs::path destination = root / name;
      if (mz_zip_reader_entry_is_dir(reader) == MZ_OK)
      {
        fs::create_directories(destination, error);
      }
      else
      {
        fs::create_directories(destination.parent_path(), error);
        if (mz_zip_reader_entry_save_file(reader, destination.c_str()) != MZ_OK)
        {
          Emulator::LogFromAnyThread(QStringLiteral("user data import: could not extract %1")
                                         .arg(QString::fromStdString(name)));
          return failure;
        }
      }
      ++done;
    } while (done < total && mz_zip_reader_goto_next_entry(reader) == MZ_OK);
  }
  fs::remove(fs::path(File::GetUserPath(D_CACHE_IDX)) / "gamelist.cache", error);
  SetProgress(total, total, QString());
  return {true, QStringLiteral("The user data has been imported.")};
}

void UserData::quitNow()
{
  // Nothing is saved on the way out: the settings in memory are the old ones.
  Emulator::LogFromAnyThread(QStringLiteral("user data imported: quitting without saving"));
  std::_Exit(0);
}

bool UserData::resetSettings()
{
  if (m_busy || !Core::IsUninitialized(Core::System::GetInstance()))
    return false;
  // NativeLibrary.ResetDolphinSettings.
  SConfig::ResetAllSettings();
  for (const QString& line : PortDefaults::Apply())
    Emulator::LogFromAnyThread(QStringLiteral("reset settings: %1").arg(line));
  Config::Save();
  Emulator::LogFromAnyThread(QStringLiteral("reset settings: done"));
  return true;
}

QString UserData::copyIn(const QString& file, const QString& folder, bool overwrite)
{
  const QFileInfo source(file);
  if (!source.isFile())
    return QStringLiteral("The file is not there any more.");
  if (!isInUserFolder(folder))
    return QStringLiteral("Choose a folder in the user data.");
  const QString target = QDir(folder).filePath(source.fileName());
  if (QFileInfo(target).absoluteFilePath() == source.absoluteFilePath())
    return QStringLiteral("The file is already there.");
  if (QFileInfo::exists(target))
  {
    if (!overwrite)
      return QStringLiteral("exists");
    QFile::remove(target);
  }
  if (!QFile::copy(file, target))
    return QStringLiteral("Could not copy the file.");
  Emulator::LogFromAnyThread(QStringLiteral("user data: copied %1 to %2").arg(file, folder));
  return {};
}

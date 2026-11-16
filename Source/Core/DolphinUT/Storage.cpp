// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/Storage.h"

#include <algorithm>
#include <vector>

#include <unistd.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>

#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Core/Config/MainSettings.h"
#include "DolphinUT/Emulator.h"

namespace
{
// Whether the games folder was added to the library (once: the user may remove it).
const Config::Info<bool> UT_GAMES_FOLDER_ADDED{{Config::System::Main, "UbuntuTouch", "GamesFolderAdded"},
                                               false};

// UICommon::FindAllGamePaths' list.
constexpr const char* GAME_EXTENSIONS[] = {"gcm",  "tgc", "bin", "iso", "ciso", "gcz", "wbfs",
                                           "wia",  "rvz", "nfs", "wad", "dol",  "elf", "json"};

QString HomeFolder(QStandardPaths::StandardLocation location)
{
  return QStandardPaths::writableLocation(location);
}

// A free name in the folder: "name.ext", else "name (2).ext", ...
QString FreeName(const QDir& folder, const QString& file_name)
{
  if (!folder.exists(file_name))
    return file_name;
  const QFileInfo info(file_name);
  const QString base = info.completeBaseName();
  const QString suffix = info.suffix().isEmpty() ? QString() : QStringLiteral(".") + info.suffix();
  for (int i = 2;; ++i)
  {
    const QString candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(suffix);
    if (!folder.exists(candidate))
      return candidate;
  }
}
}  // namespace

QString Storage::PackageName()
{
  const QString app_id = qEnvironmentVariable("APP_ID");
  return app_id.section(QLatin1Char('_'), 0, 0);
}

void Storage::ConfigureApplication()
{
  const QString package = PackageName();
  if (package.isEmpty())
  {
    QCoreApplication::setOrganizationName(QStringLiteral("Dolphin Emulator"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("dolphin-emu.org"));
    QCoreApplication::setApplicationName(QStringLiteral("dolphin-ut"));
    return;
  }
  // As Lomiri apps do (MainView.applicationName): ~/.config/<package>/<package>.conf.
  QCoreApplication::setOrganizationName(package);
  QCoreApplication::setOrganizationDomain(QString());
  QCoreApplication::setApplicationName(package);
}

std::string Storage::UserFolder(const std::string& from_command_line)
{
  if (!from_command_line.empty())
    return from_command_line;
  const QString package = PackageName();
  if (package.isEmpty())
    return {};
  return (HomeFolder(QStandardPaths::GenericDataLocation) + QLatin1Char('/') + package)
      .toStdString();
}

QString Storage::GamesFolderPath()
{
  return HomeFolder(QStandardPaths::DocumentsLocation) + QStringLiteral("/Dolphin/Games");
}

QString Storage::ScreenshotsFolderPath()
{
  return HomeFolder(QStandardPaths::PicturesLocation) + QStringLiteral("/Dolphin");
}

bool Storage::IsGameFile(const QString& path)
{
  const QString suffix = QFileInfo(path).suffix().toLower();
  return std::ranges::any_of(GAME_EXTENSIONS,
                             [&suffix](const char* ext) { return suffix == QLatin1String(ext); });
}

void Storage::SetUp()
{
  const QString games = GamesFolderPath();
  if (!QDir().mkpath(games))
    Emulator::LogFromAnyThread(QStringLiteral("storage: cannot create %1").arg(games));

  ApplyScreenshotsFolder();

  if (!Config::Get(UT_GAMES_FOLDER_ADDED))
  {
    std::vector<std::string> folders = Config::GetIsoPaths();
    if (std::ranges::find(folders, games.toStdString()) == folders.end())
    {
      folders.push_back(games.toStdString());
      Config::SetIsoPaths(folders);
    }
    Config::SetBase(UT_GAMES_FOLDER_ADDED, true);
    Config::Save();
  }

  Emulator::LogFromAnyThread(
      QStringLiteral("storage: user folder %1, games %2, screenshots %3 (APP_ID %4)")
          .arg(QString::fromStdString(File::GetUserPath(D_USER_IDX)), games,
               QString::fromStdString(File::GetUserPath(D_SCREENSHOTS_IDX)),
               qEnvironmentVariable("APP_ID", QStringLiteral("not set"))));
}

void Storage::ApplyScreenshotsFolder()
{
  // Dolphin puts screenshots in <folder>/<game ID>/ (Core::SaveScreenShot).
  const QString screenshots = ScreenshotsFolderPath();
  if (QDir().mkpath(screenshots))
    File::SetUserPath(D_SCREENSHOTS_IDX, screenshots.toStdString());
  else
    Emulator::LogFromAnyThread(QStringLiteral("storage: cannot create %1, screenshots stay in %2")
                                   .arg(screenshots, QString::fromStdString(
                                                         File::GetUserPath(D_SCREENSHOTS_IDX))));
}

Storage::Storage(QObject* parent) : QObject(parent)
{
}

Storage::~Storage()
{
  if (m_import_thread.joinable())
    m_import_thread.join();
}

QString Storage::GetAppId() const
{
  return qEnvironmentVariable("APP_ID");
}

QString Storage::GetUserFolder() const
{
  QString folder = QString::fromStdString(File::GetUserPath(D_USER_IDX));
  if (folder.endsWith(QLatin1Char('/')))
    folder.chop(1);
  return folder;
}

QVariantList Storage::places() const
{
  QVariantList result;
  const auto add = [&result](const QString& name, const QString& path, const QString& icon) {
    if (QFileInfo(path).isDir())
      result.append(QVariantMap{{QStringLiteral("name"), name},
                                {QStringLiteral("path"), path},
                                {QStringLiteral("icon"), icon}});
  };
  add(QStringLiteral("Dolphin games"), GamesFolderPath(), QStringLiteral("folder"));
  add(QStringLiteral("Documents"), HomeFolder(QStandardPaths::DocumentsLocation),
      QStringLiteral("document-open"));
  add(QStringLiteral("Downloads"), HomeFolder(QStandardPaths::DownloadLocation),
      QStringLiteral("save-to"));
  add(QStringLiteral("Home"), HomeFolder(QStandardPaths::HomeLocation), QStringLiteral("home"));
  // microSD cards and USB drives: udisks mounts them under /media/<user>/.
  for (const QStorageInfo& volume : QStorageInfo::mountedVolumes())
  {
    const QString root = volume.rootPath();
    if (volume.isValid() && volume.isReady() && root.startsWith(QStringLiteral("/media/")))
    {
      const QString name = volume.displayName().isEmpty() ? QFileInfo(root).fileName() :
                                                            volume.displayName();
      add(name, root, QStringLiteral("sdcard-symbolic"));
    }
  }
  return result;
}

bool Storage::importFiles(const QVariantList& urls)
{
  if (m_importing)
    return false;
  QStringList paths;
  for (const QVariant& value : urls)
  {
    const QUrl url = value.toUrl();
    paths.append(url.isLocalFile() ? url.toLocalFile() : value.toString());
  }
  if (m_import_thread.joinable())
    m_import_thread.join();
  m_importing = true;
  emit importingChanged();
  m_import_thread = std::thread([this, paths] {
    QVariantMap result = ImportFiles(paths);
    QMetaObject::invokeMethod(this, [this, result = std::move(result)] {
      m_importing = false;
      emit importingChanged();
      emit importFinished(result);
    });
  });
  return true;
}

QVariantMap Storage::ImportFiles(const QStringList& paths)
{
  QStringList imported;
  QVariantList skipped;
  const auto skip = [&skipped](const QString& name, const QString& reason) {
    skipped.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("reason"), reason}});
    Emulator::LogFromAnyThread(QStringLiteral("import: %1 skipped: %2").arg(name, reason));
  };

  const QDir games(GamesFolderPath());
  QDir().mkpath(games.path());
  for (const QString& path : paths)
  {
    const QFileInfo source(path);
    const QString name = source.fileName();
    if (!source.isFile())
    {
      skip(name.isEmpty() ? path : name, QStringLiteral("not a file"));
      continue;
    }
    if (!IsGameFile(path))
    {
      skip(name, QStringLiteral("not a game file"));
      continue;
    }
    const QString target = games.filePath(FreeName(games, name));
    // The same file system (content-hub's store is in ~/.cache): a rename, even for a 4 GB disc.
    const char* how = "moved";
    bool ok = QFile::rename(path, target);
    if (!ok)
    {
      how = "linked";
      ok = link(QFile::encodeName(path).constData(), QFile::encodeName(target).constData()) == 0;
    }
    if (!ok)
    {
      how = "copied";
      ok = QFile::copy(path, target);
    }
    if (!ok)
    {
      skip(name, QStringLiteral("could not be moved to %1").arg(games.path()));
      continue;
    }
    imported.append(QFileInfo(target).fileName());
    Emulator::LogFromAnyThread(
        QStringLiteral("import: %1 %2 to %3").arg(path, QLatin1String(how), target));
  }
  return {{QStringLiteral("imported"), imported}, {QStringLiteral("skipped"), skipped}};
}

bool Storage::exists(const QString& path) const
{
  return QFileInfo::exists(path);
}

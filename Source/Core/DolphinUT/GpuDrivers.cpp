// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/GpuDrivers.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/MinizipUtil.h"
#include "Common/ScopeGuard.h"
#include "DolphinUT/VulkanDriver.h"

namespace
{
const Config::Info<std::string> UT_GPU_DRIVER{{Config::System::Main, "UbuntuTouch", "GpuDriver"},
                                              ""};

// The name the stand-in libvndksupport.so loads from the driver's folder (ut/vkdriver).
constexpr const char* DRIVER_FILE = "vulkan.adreno.so";
// Packages are a driver and the libraries it needs; anything bigger is not one.
constexpr u64 MAX_FILE_SIZE = 256 * 1024 * 1024;

QString s_active;
QString s_active_error;

std::string DriversFolder()
{
  return File::GetUserPath(D_USER_IDX) + "GpuDrivers/";
}

std::string ShimFolder()
{
  return File::GetExeDirectory() + "/lib/hybris";
}

// A folder name from the package's name and version (letters, digits, '.', '-', '_').
QString MakeId(const QString& name, const QString& version)
{
  QString id = version.isEmpty() ? name : name + QStringLiteral("-v") + version;
  for (QChar& c : id)
  {
    if (!c.isLetterOrNumber() && c != u'.' && c != u'-' && c != u'_')
      c = u'_';
  }
  while (id.startsWith(u'.'))
    id.remove(0, 1);
  return id.left(80);
}

// A library file inside the package: a plain name at the top level.
bool IsPlainName(const std::string& name)
{
  return !name.empty() && name.find('/') == std::string::npos && name != "." && name != "..";
}

// An arm64 ELF shared library (the phone's Android libraries are; a driver for another
// architecture would only fail later, inside libhybris).
bool IsArm64Library(const std::vector<u8>& data)
{
  constexpr u16 EM_AARCH64 = 183;
  if (data.size() < 64 || std::memcmp(data.data(), "\x7f" "ELF", 4) != 0)
    return false;
  const bool is_64_bit = data[4] == 2;
  const bool little_endian = data[5] == 1;
  const u16 machine = static_cast<u16>(data[18] | (data[19] << 8));
  return is_64_bit && little_endian && machine == EM_AARCH64;
}

QVariantMap ReadMetadata(const QString& folder)
{
  QFile file(folder + QStringLiteral("/meta.json"));
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}
}  // namespace

std::string GpuDrivers::ApplyAtStart()
{
  std::string folder;
  if (const char* from_environment = std::getenv("DOLPHIN_UT_GPU_DRIVER");
      from_environment && *from_environment)
  {
    folder = from_environment;
    s_active = QString::fromStdString(folder);
  }
  else
  {
    // Dolphin's config system starts later (UICommon::Init), so read its file directly.
    Common::IniFile ini;
    ini.Load(File::GetUserPath(F_DOLPHINCONFIG_IDX));
    std::string id;
    ini.GetIfExists("UbuntuTouch", "GpuDriver", &id);
    if (id.empty())
      return {};
    folder = DriversFolder() + id;
    s_active = QString::fromStdString(id);
  }

  std::string error;
  if (!VulkanDriver::Use(folder, ShimFolder(), &error))
  {
    s_active.clear();
    s_active_error = QString::fromStdString(error);
    return "GPU driver " + folder + ": not in use, " + error;
  }
  return "GPU driver " + folder + ": in use";
}

GpuDrivers::GpuDrivers(QObject* parent) : QObject(parent)
{
}

QString GpuDrivers::GetFolder() const
{
  return QString::fromStdString(DriversFolder());
}

QVariantList GpuDrivers::GetDrivers() const
{
  QVariantList drivers;
  const QDir folder(GetFolder());
  for (const QString& id : folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
  {
    if (!QFileInfo::exists(folder.filePath(id) + QLatin1Char('/') + QLatin1String(DRIVER_FILE)))
      continue;
    const QVariantMap meta = ReadMetadata(folder.filePath(id));
    drivers.append(QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), meta.value(QStringLiteral("name"), id)},
        {QStringLiteral("version"), meta.value(QStringLiteral("packageVersion"))},
        {QStringLiteral("driverVersion"), meta.value(QStringLiteral("driverVersion"))},
        {QStringLiteral("vendor"), meta.value(QStringLiteral("vendor"))},
        {QStringLiteral("author"), meta.value(QStringLiteral("author"))},
        {QStringLiteral("description"), meta.value(QStringLiteral("description"))},
    });
  }
  return drivers;
}

QString GpuDrivers::GetSelected() const
{
  return QString::fromStdString(Config::Get(UT_GPU_DRIVER));
}

QString GpuDrivers::GetActive() const
{
  return s_active;
}

QString GpuDrivers::GetActiveError() const
{
  return s_active_error;
}

bool GpuDrivers::IsDriverPackage(const QString& path)
{
  if (!path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
    return false;
  void* zip = mz_zip_reader_create();
  if (!zip)
    return false;
  Common::ScopeGuard zip_guard{[&] { mz_zip_reader_delete(&zip); }};
  if (mz_zip_reader_open_file(zip, path.toStdString().c_str()) != MZ_OK)
    return false;
  const bool found = mz_zip_reader_locate_entry(zip, "meta.json", 0) == MZ_OK;
  mz_zip_reader_close(zip);
  return found;
}

QVariantMap GpuDrivers::install(const QString& path)
{
  const auto failed = [](const QString& error) {
    return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
  };

  void* zip = mz_zip_reader_create();
  if (!zip)
    return failed(QStringLiteral("Could not open the package."));
  Common::ScopeGuard zip_guard{[&] { mz_zip_reader_delete(&zip); }};
  if (mz_zip_reader_open_file(zip, path.toStdString().c_str()) != MZ_OK)
    return failed(QStringLiteral("This is not a zip file."));

  // meta.json, as the Android app reads it (GpuDriverMetadata, schema version 1).
  if (mz_zip_reader_locate_entry(zip, "meta.json", 0) != MZ_OK)
    return failed(QStringLiteral("This is not a driver package: it has no meta.json."));
  mz_zip_file* info = nullptr;
  if (mz_zip_reader_entry_get_info(zip, &info) != MZ_OK || info->uncompressed_size > 65536)
    return failed(QStringLiteral("The package's meta.json cannot be read."));
  std::string meta_text(info->uncompressed_size, '\0');
  if (!Common::ReadFileFromZip(zip, &meta_text))
    return failed(QStringLiteral("The package's meta.json cannot be read."));
  const QJsonObject meta = QJsonDocument::fromJson(QByteArray::fromStdString(meta_text)).object();
  const QString name = meta.value(QStringLiteral("name")).toString();
  const std::string library = meta.value(QStringLiteral("libraryName")).toString().toStdString();
  if (meta.value(QStringLiteral("schemaVersion")).toInt() != 1 || name.isEmpty() ||
      !IsPlainName(library))
  {
    return failed(QStringLiteral("The package's meta.json is not one this app knows."));
  }

  // The package's top-level files: meta.json, the driver (saved as vulkan.adreno.so) and any
  // libraries it brings, which the driver's own namespace finds in the same folder.
  struct Entry
  {
    std::string name;
    std::vector<u8> data;
  };
  std::vector<Entry> entries;
  bool found_driver = false;
  if (mz_zip_reader_goto_first_entry(zip) == MZ_OK)
  {
    do
    {
      if (mz_zip_reader_entry_get_info(zip, &info) != MZ_OK)
        return failed(QStringLiteral("The package is damaged."));
      const std::string entry_name = info->filename;
      const bool is_library =
          entry_name == library || (IsPlainName(entry_name) && entry_name.ends_with(".so"));
      if (mz_zip_reader_entry_is_dir(zip) == MZ_OK || !is_library)
        continue;
      if (info->uncompressed_size > MAX_FILE_SIZE)
        return failed(QStringLiteral("The package's %1 is too big.").arg(QString::fromStdString(entry_name)));
      Entry entry{entry_name == library ? DRIVER_FILE : entry_name,
                  std::vector<u8>(info->uncompressed_size)};
      if (!Common::ReadFileFromZip(zip, &entry.data))
        return failed(QStringLiteral("The package's %1 cannot be read.").arg(QString::fromStdString(entry_name)));
      if (!IsArm64Library(entry.data))
      {
        return failed(QStringLiteral("%1 is not a library for this phone (64-bit ARM).")
                          .arg(QString::fromStdString(entry_name)));
      }
      found_driver |= entry_name == library;
      entries.push_back(std::move(entry));
    } while (mz_zip_reader_goto_next_entry(zip) == MZ_OK);
  }
  if (!found_driver)
  {
    return failed(QStringLiteral("The package has no %1, which its meta.json names.")
                      .arg(QString::fromStdString(library)));
  }

  // Written to a new folder, then put in place, so a failed install leaves nothing half-done.
  const QString id = MakeId(name, meta.value(QStringLiteral("packageVersion")).toString());
  const QString target = GetFolder() + id;
  const QString temporary = GetFolder() + QStringLiteral(".installing");
  QDir(temporary).removeRecursively();
  if (!QDir().mkpath(temporary))
    return failed(QStringLiteral("Cannot write to %1.").arg(GetFolder()));
  entries.push_back({"meta.json", std::vector<u8>(meta_text.begin(), meta_text.end())});
  for (const Entry& entry : entries)
  {
    QFile file(temporary + QLatin1Char('/') + QString::fromStdString(entry.name));
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(reinterpret_cast<const char*>(entry.data.data()),
                   static_cast<qint64>(entry.data.size())) != static_cast<qint64>(entry.data.size()))
    {
      QDir(temporary).removeRecursively();
      return failed(QStringLiteral("Cannot write the driver (is the phone's storage full?)."));
    }
  }
  // Replacing an installed copy, even the one in use: its files stay open until the app quits.
  QDir(target).removeRecursively();
  if (!QDir().rename(temporary, target))
  {
    QDir(temporary).removeRecursively();
    return failed(QStringLiteral("Cannot write to %1.").arg(GetFolder()));
  }
  emit changed();
  return QVariantMap{{QStringLiteral("ok"), true}, {QStringLiteral("id"), id},
                     {QStringLiteral("name"), name}};
}

void GpuDrivers::select(const QString& id)
{
  Config::SetBase(UT_GPU_DRIVER, id.toStdString());
  Config::Save();
  emit changed();
}

bool GpuDrivers::remove(const QString& id)
{
  if (id.isEmpty() || id.contains(u'/') || id.startsWith(u'.'))
    return false;
  if (GetSelected() == id)
  {
    Config::SetBase(UT_GPU_DRIVER, std::string());
    Config::Save();
  }
  const bool removed = QDir(GetFolder() + id).removeRecursively();
  emit changed();
  return removed;
}

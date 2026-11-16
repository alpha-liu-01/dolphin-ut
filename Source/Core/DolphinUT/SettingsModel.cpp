// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/SettingsModel.h"

#include <memory>
#include <type_traits>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include "Common/CommonTypes.h"
#include "Common/FatFsUtil.h"
#include "Common/FileUtil.h"
#include "Core/Config/AchievementSettings.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/Config/UISettings.h"
#include "Core/ConfigLoaders/GameConfigLoader.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "DolphinUT/CpuBoost.h"
#include "DolphinUT/Emulator.h"
#include "DolphinUT/Storage.h"
#include "UICommon/UICommon.h"

namespace
{
using Binding = SettingsModel::Binding;

template <typename T>
QVariant ToVariant(const T& value)
{
  if constexpr (std::is_enum_v<T>)
    return static_cast<int>(value);
  else if constexpr (std::is_same_v<T, std::string>)
    return QString::fromStdString(value);
  else if constexpr (std::is_same_v<T, bool>)
    return value;
  else if constexpr (std::is_floating_point_v<T>)
    return static_cast<double>(value);
  else
    return static_cast<qlonglong>(value);
}

template <typename T>
T FromVariant(const QVariant& value)
{
  if constexpr (std::is_enum_v<T>)
    return static_cast<T>(value.toInt());
  else if constexpr (std::is_same_v<T, std::string>)
    return value.toString().toStdString();
  else if constexpr (std::is_same_v<T, bool>)
    return value.toBool();
  else if constexpr (std::is_floating_point_v<T>)
    return static_cast<T>(value.toDouble());
  else
    return static_cast<T>(value.toLongLong());
}

template <typename T>
void Write(const Config::Info<T>& info, bool game, const T& value)
{
  if (game)
    Config::Set(Config::LayerType::LocalGame, info, value);
  else
    Config::SetBaseOrCurrent(info, value);
}

template <typename T>
bool InGameLayer(const Config::Info<T>& info)
{
  const auto layer = Config::GetLayer(Config::LayerType::LocalGame);
  return layer && layer->Exists(info.GetLocation());
}

template <typename T>
void Delete(const Config::Info<T>& info, bool game)
{
  Config::DeleteKey(game ? Config::LayerType::LocalGame : Config::LayerType::Base, info);
}

// Dolphin's own setting (a global Config::Info).
template <typename T>
Binding Bind(const Config::Info<T>& info)
{
  return {[&info] { return ToVariant(Config::Get(info)); },
          [&info](bool game, const QVariant& value) { Write(info, game, FromVariant<T>(value)); },
          [&info] { return InGameLayer(info); }, [&info](bool game) { Delete(info, game); }};
}

// A setting Dolphin does not define (the Android app's definition).
template <typename T>
Binding Bind(std::shared_ptr<Config::Info<T>> info)
{
  return {[info] { return ToVariant(Config::Get(*info)); },
          [info](bool game, const QVariant& value) { Write(*info, game, FromVariant<T>(value)); },
          [info] { return InGameLayer(*info); }, [info](bool game) { Delete(*info, game); }};
}

// Settings the Android app shows through converter objects (SettingsFragmentPresenter's
// AbstractIntSetting instances), and the port's CPU boost.
void AddVirtualSettings(std::map<std::string, Binding>& bindings)
{
  bindings["Virtual.CPU_BOOST"] = Bind(CpuBoost::UT_CPU_BOOST);

  // DSP engine: 0 HLE, 1 LLE recompiler, 2 LLE interpreter (DSPHLE, DSP.EnableJIT).
  bindings["Virtual.DSP_ENGINE"] = {
      [] {
        if (Config::Get(Config::MAIN_DSP_HLE))
          return QVariant(0);
        return QVariant(Config::Get(Config::MAIN_DSP_JIT) ? 1 : 2);
      },
      [](bool game, const QVariant& value) {
        const int engine = value.toInt();
        Write(Config::MAIN_DSP_HLE, game, engine == 0);
        Write(Config::MAIN_DSP_JIT, game, engine != 2);
      },
      [] { return InGameLayer(Config::MAIN_DSP_HLE) || InGameLayer(Config::MAIN_DSP_JIT); },
      [](bool game) {
        Delete(Config::MAIN_DSP_HLE, game);
        Delete(Config::MAIN_DSP_JIT, game);
      }};

  // Synchronize GPU thread: 0 never, 1 on idle skipping, 2 always (SyncOnSkipIdle, SyncGPU).
  bindings["Virtual.SYNC_GPU"] = {
      [] {
        if (Config::Get(Config::MAIN_SYNC_GPU))
          return QVariant(2);
        return QVariant(Config::Get(Config::MAIN_SYNC_ON_SKIP_IDLE) ? 1 : 0);
      },
      [](bool game, const QVariant& value) {
        const int sync = value.toInt();
        Write(Config::MAIN_SYNC_ON_SKIP_IDLE, game, sync == 1);
        Write(Config::MAIN_SYNC_GPU, game, sync == 2);
      },
      [] {
        return InGameLayer(Config::MAIN_SYNC_GPU) || InGameLayer(Config::MAIN_SYNC_ON_SKIP_IDLE);
      },
      [](bool game) {
        Delete(Config::MAIN_SYNC_GPU, game);
        Delete(Config::MAIN_SYNC_ON_SKIP_IDLE, game);
      }};

  // Emulated memory sizes, stored in bytes, shown in MB.
  const auto megabytes = [](const Config::Info<u32>& info) {
    return Binding{
        [&info] { return QVariant(static_cast<int>(Config::Get(info) / 0x100000)); },
        [&info](bool game, const QVariant& value) {
          Write(info, game, static_cast<u32>(value.toInt()) * 0x100000u);
        },
        [&info] { return InGameLayer(info); }, [&info](bool game) { Delete(info, game); }};
  };
  bindings["Virtual.MEM1_MB"] = megabytes(Config::MAIN_MEM1_SIZE);
  bindings["Virtual.MEM2_MB"] = megabytes(Config::MAIN_MEM2_SIZE);
}
// The paths (Config → Paths, GameCube, Wii; stage 3.7). Most are also Dolphin's user paths, which
// UICommon::Init sets from these settings at start; a change applies them at once, as DolphinQt's
// ConfigUserPath does. Empty means Dolphin's default folder under the user folder.
struct PathSetting
{
  const char* name;
  const Config::Info<std::string>& info;
  int user_path;  // -1: only the setting
};
const PathSetting PATH_SETTINGS[] = {
    {"StringSetting.MAIN_DEFAULT_ISO", Config::MAIN_DEFAULT_ISO, -1},
    {"StringSetting.MAIN_FS_PATH", Config::MAIN_FS_PATH, D_WIIROOT_IDX},
    {"StringSetting.MAIN_DUMP_PATH", Config::MAIN_DUMP_PATH, D_DUMP_IDX},
    {"StringSetting.MAIN_LOAD_PATH", Config::MAIN_LOAD_PATH, D_LOAD_IDX},
    {"StringSetting.MAIN_RESOURCEPACK_PATH", Config::MAIN_RESOURCEPACK_PATH, D_RESOURCEPACK_IDX},
    {"StringSetting.MAIN_WFS_PATH", Config::MAIN_WFS_PATH, D_WFSROOT_IDX},
    {"StringSetting.MAIN_WII_SD_CARD_IMAGE_PATH", Config::MAIN_WII_SD_CARD_IMAGE_PATH,
     F_WIISDCARDIMAGE_IDX},
    {"StringSetting.MAIN_WII_SD_CARD_SYNC_FOLDER_PATH", Config::MAIN_WII_SD_CARD_SYNC_FOLDER_PATH,
     D_WIISDCARDSYNCFOLDER_IDX},
    {"StringSetting.MAIN_GBA_BIOS_PATH", Config::MAIN_GBA_BIOS_PATH, F_GBABIOS_IDX},
    {"StringSetting.MAIN_GBA_SAVES_PATH", Config::MAIN_GBA_SAVES_PATH, D_GBASAVES_IDX},
    {"StringSetting.MAIN_GB_PLAYER_ROM", Config::MAIN_GBA_ROM_PATHS[4], -1},
};

// Dolphin's user paths from scratch: the defaults under the user folder, then each configured path
// (as UICommon::Init does at start), the folders Dolphin expects, and the port's screenshot folder.
void ApplyUserPaths()
{
  File::SetUserPath(D_USER_IDX, File::GetUserPath(D_USER_IDX));
  for (const PathSetting& path : PATH_SETTINGS)
  {
    const std::string value = Config::Get(path.info);
    if (path.user_path >= 0 && !value.empty())
      File::SetUserPath(path.user_path, value);
  }
  UICommon::CreateDirectories();
  File::CreateFullPath(File::GetUserPath(D_WIISDCARDSYNCFOLDER_IDX));
  File::CreateFullPath(File::GetUserPath(D_GBASAVES_IDX));
  File::CreateFullPath(File::GetUserPath(D_DUMPFRAMES_IDX));
  File::CreateFullPath(File::GetUserPath(D_DUMPAUDIO_IDX));
  File::CreateFullPath(File::GetUserPath(D_RIIVOLUTION_IDX));
  File::CreateFullPath(File::GetUserPath(D_DYNAMICINPUT_IDX));
  Storage::ApplyScreenshotsFolder();
}

void AddPathSettings(std::map<std::string, Binding>& bindings)
{
  for (const PathSetting& path : PATH_SETTINGS)
  {
    const Config::Info<std::string>& info = path.info;
    const int user_path = path.user_path;
    bindings[path.name] = {
        // The path in use: the setting, or Dolphin's default (as ConfigUserPath shows it).
        [&info, user_path] {
          std::string value = Config::Get(info);
          if (value.empty() && user_path >= 0)
          {
            value = File::GetUserPath(user_path);
            if (value.ends_with('/'))
              value.pop_back();
          }
          return QVariant(QString::fromStdString(value));
        },
        [&info, user_path](bool, const QVariant& value) {
          // Global only (the items are globalOnly). "" goes back to the default.
          const std::string text = value.toString().trimmed().toStdString();
          if (text.empty())
            Config::DeleteKey(Config::LayerType::Base, info);
          else
            Config::SetBase(info, text);
          if (user_path >= 0)
            ApplyUserPaths();
        },
        [&info] { return InGameLayer(info); }, [&info](bool game) { Delete(info, game); }};
  }
}
}  // namespace

SettingsModel::SettingsModel(QObject* parent) : QObject(parent)
{
  QFile file(QStringLiteral(":/settings/settings.json"));
  if (file.open(QIODevice::ReadOnly))
    m_menus = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("menus")).toObject();

  m_bindings = {
#include "DolphinUT/SettingsTable.inc"
  };
  AddVirtualSettings(m_bindings);
  AddPathSettings(m_bindings);
}

SettingsModel::~SettingsModel()
{
  if (m_action_thread.joinable())
    m_action_thread.join();
}

bool SettingsModel::runAction(const QString& action)
{
  std::function<bool()> work;
  if (action == QStringLiteral("sd_pack"))
    work = [] { return Common::SyncSDFolderToSDImage([] { return false; }, false); };
  else if (action == QStringLiteral("sd_unpack"))
    work = [] { return Common::SyncSDImageToSDFolder([] { return false; }); };
  if (!work || m_busy)
    return false;
  if (m_action_thread.joinable())
    m_action_thread.join();
  m_busy = true;
  emit busyChanged();
  Emulator::LogFromAnyThread(QStringLiteral("action %1").arg(action));
  m_action_thread = std::thread([this, action, work = std::move(work)] {
    const bool ok = work();
    QMetaObject::invokeMethod(this, [this, action, ok] {
      Emulator::LogFromAnyThread(QStringLiteral("action %1: %2").arg(action, ok ? "done" : "failed"));
      m_busy = false;
      emit busyChanged();
      emit actionFinished(action, ok);
      Changed();
    });
  });
  return true;
}

QVariantMap SettingsModel::menu(const QString& tag) const
{
  return m_menus.value(tag).toObject().toVariantMap();
}

QVariant SettingsModel::value(const QString& name) const
{
  const auto it = m_bindings.find(name.toStdString());
  return it != m_bindings.end() ? it->second.get() : QVariant();
}

void SettingsModel::setValue(const QString& name, const QVariant& value)
{
  const auto it = m_bindings.find(name.toStdString());
  if (it == m_bindings.end())
    return;
  const bool game = !m_game_id.isEmpty();
  it->second.set(game, value);
  if (game)
    Config::GetLayer(Config::LayerType::LocalGame)->Save();
  else
    Config::Save();
  Emulator::LogFromAnyThread(QStringLiteral("setting %1 = %2%3")
                                 .arg(name, value.toString(),
                                      game ? QStringLiteral(" (game %1)").arg(m_game_id) : QString()));
  Changed();
}

bool SettingsModel::overridden(const QString& name) const
{
  if (m_game_id.isEmpty())
    return false;
  const auto it = m_bindings.find(name.toStdString());
  return it != m_bindings.end() && it->second.in_game_layer();
}

void SettingsModel::clear(const QString& name)
{
  const auto it = m_bindings.find(name.toStdString());
  if (it == m_bindings.end() || m_game_id.isEmpty())
    return;
  it->second.clear(true);
  Config::GetLayer(Config::LayerType::LocalGame)->Save();
  Changed();
}

bool SettingsModel::openGame(const QString& game_id, int revision)
{
  // While a game runs, its own layer is in use (and the Android app does not edit another game's).
  if (!Core::IsUninitialized(Core::System::GetInstance()) || game_id.isEmpty())
    return false;
  closeGame();
  Config::AddLayer(ConfigLoaders::GenerateLocalGameConfigLoader(game_id.toStdString(),
                                                                static_cast<u16>(revision)));
  m_game_id = game_id;
  emit gameChanged();
  Changed();
  return true;
}

void SettingsModel::closeGame()
{
  if (m_game_id.isEmpty())
    return;
  if (const auto layer = Config::GetLayer(Config::LayerType::LocalGame))
    layer->Save();
  Config::RemoveLayer(Config::LayerType::LocalGame);
  m_game_id.clear();
  emit gameChanged();
  Changed();
}

bool SettingsModel::clearGameSettings(const QString& game_id)
{
  if (game_id.isEmpty())
    return false;
  const std::string path =
      File::GetUserPath(D_GAMESETTINGS_IDX) + game_id.toStdString() + ".ini";
  return File::Exists(path) && File::Delete(path);
}

void SettingsModel::Changed()
{
  ++m_revision;
  emit changed();
}

// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/SettingsModel.h"

#include <filesystem>
#include <memory>
#include <system_error>
#include <type_traits>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>

#include <fmt/format.h>

#include "Common/CommonTypes.h"
#include "Common/FatFsUtil.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/Logging/Log.h"
#include "Common/Logging/LogManager.h"
#include "Core/Config/AchievementSettings.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/Config/UISettings.h"
#include "Core/ConfigLoaders/GameConfigLoader.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/System.h"
#include "DolphinUT/CpuBoost.h"
#include "DolphinUT/Emulator.h"
#include "DolphinUT/Storage.h"
#include "UICommon/UICommon.h"
#include "VideoCommon/PostProcessing.h"
#include "VideoCommon/VideoConfig.h"

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
// The log (stage 6.2). Dolphin's LogManager reads the Logger file once, at start, and writes its own
// state back at exit (LogManager::Shutdown), so a change also goes to the LogManager, as DolphinQt's
// LogConfigWidget does (the Android app re-creates the LogManager instead). The verbosity needs
// nothing more: the LogManager follows it through a config callback. The log is the app's: the
// items are global only, so these always write the base layer.
using Common::Log::LogManager;

Config::Info<bool> LogTypeInfo(const std::string& short_name)
{
  return {{Config::System::Logger, "Logs", short_name}, false};
}

void SetLogType(Common::Log::LogType type, const std::string& short_name, bool enabled)
{
  Config::SetBaseOrCurrent(LogTypeInfo(short_name), enabled);
  if (auto* const log_manager = LogManager::GetInstance())
    log_manager->SetEnable(type, enabled);
}

void AddLogSettings(std::map<std::string, Binding>& bindings)
{
  Binding& to_file = bindings["BooleanSetting.LOGGER_WRITE_TO_FILE"];
  to_file.set = [set = to_file.set](bool, const QVariant& value) {
    set(false, value);
    if (auto* const log_manager = LogManager::GetInstance())
      log_manager->EnableListener(Common::Log::LogListener::FILE_LISTENER, value.toBool());
  };

  auto* const log_manager = LogManager::GetInstance();
  if (!log_manager)
    return;
  const auto types = log_manager->GetLogTypes();
  for (std::size_t i = 0; i < types.size(); ++i)
  {
    const auto type = static_cast<Common::Log::LogType>(i);
    const std::string short_name = types[i].m_short_name;
    bindings["LogType." + short_name] = {
        [short_name] { return QVariant(Config::Get(LogTypeInfo(short_name))); },
        [type, short_name](bool, const QVariant& value) {
          SetLogType(type, short_name, value.toBool());
        },
        [] { return false; }, [](bool) {}};
  }
}

// Actions on the UI thread, as the Android app runs them (quick, or they pause the emulation, which
// a worker thread racing the UI's reads of the settings should not do): {ok, message}.
std::pair<bool, QString> RunQuickAction(const QString& action)
{
  if (action == QStringLiteral("log_enable_all") || action == QStringLiteral("log_disable_all"))
  {
    // SettingsFragmentPresenter.setAllLogTypes.
    const bool enabled = action == QStringLiteral("log_enable_all");
    if (auto* const log_manager = LogManager::GetInstance())
    {
      const auto types = log_manager->GetLogTypes();
      for (std::size_t i = 0; i < types.size(); ++i)
        SetLogType(static_cast<Common::Log::LogType>(i), types[i].m_short_name, enabled);
    }
    Config::Save();
    return {true, {}};
  }
  if (action == QStringLiteral("log_clear"))
  {
    // SettingsAdapter.clearLog: emptied, not deleted, in case something watches it. The log is
    // written in append mode, so it carries on at the new end.
    const std::string path = File::GetUserPath(F_MAINLOG_IDX);
    if (!File::Exists(path))
      return {true, {}};
    std::error_code error;
    std::filesystem::resize_file(path, 0, error);
    if (error)
    {
      return {false, QStringLiteral("Failed to clear the log file: %1")
                         .arg(QString::fromStdString(error.message()))};
    }
    return {true, {}};
  }
  if (action == QStringLiteral("jit_wipe_profiling") ||
      action == QStringLiteral("jit_write_block_log"))
  {
    // NativeLibrary.WipeJitBlockProfilingData / WriteJitBlockLogDump (Source/Android/jni).
    auto& system = Core::System::GetInstance();
    auto& jit_interface = system.GetJitInterface();
    const Core::CPUThreadGuard cpu_guard(system);
    if (jit_interface.GetCore() == nullptr)
      return {false, QStringLiteral("JIT is not active")};
    if (action == QStringLiteral("jit_wipe_profiling"))
    {
      jit_interface.WipeBlockProfilingData(cpu_guard);
      return {true, {}};
    }
    const std::string folder = File::GetUserPath(D_DUMPDEBUG_JITBLOCKS_IDX);
    File::CreateFullPath(folder);
    const std::string filename =
        fmt::format("{}{}.txt", folder, SConfig::GetInstance().GetGameID());
    File::IOFile file(filename, "w");
    if (!file)
    {
      return {false, QStringLiteral("Failed to open \"%1\" for writing.")
                         .arg(QString::fromStdString(filename))};
    }
    jit_interface.JitBlockLogDump(cpu_guard, file.GetHandle());
    return {true, QStringLiteral("Wrote to \"%1\".").arg(QString::fromStdString(filename))};
  }
  return {false, {}};
}

bool IsQuickAction(const QString& action)
{
  return action.startsWith(QStringLiteral("log_")) || action.startsWith(QStringLiteral("jit_"));
}
}  // namespace

SettingsModel::SettingsModel(QObject* parent) : QObject(parent)
{
  QFile file(QStringLiteral(":/settings/settings.json"));
  if (file.open(QIODevice::ReadOnly))
  {
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    m_menus = root.value(QStringLiteral("menus")).toObject();
    const QJsonObject settings = root.value(QStringLiteral("settings")).toObject();
    for (auto it = settings.begin(); it != settings.end(); ++it)
    {
      if (it.value().toObject().value(QStringLiteral("lockedWhileRunning")).toBool())
        m_locked_while_running.insert(it.key().toStdString());
    }
  }

  m_bindings = {
#include "DolphinUT/SettingsTable.inc"
  };
  AddVirtualSettings(m_bindings);
  AddPathSettings(m_bindings);
  AddLogSettings(m_bindings);
}

SettingsModel::~SettingsModel()
{
  if (m_action_thread.joinable())
    m_action_thread.join();
}

bool SettingsModel::runAction(const QString& action)
{
  if (IsQuickAction(action))
  {
    if (m_busy)
      return false;
    Emulator::LogFromAnyThread(QStringLiteral("action %1").arg(action));
    const auto [ok, message] = RunQuickAction(action);
    Emulator::LogFromAnyThread(QStringLiteral("action %1: %2%3")
                                   .arg(action, ok ? "done" : "failed",
                                        message.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(message)));
    // Queued: the page marks itself as waiting once this returns.
    QMetaObject::invokeMethod(
        this,
        [this, action, ok, message] {
          emit actionFinished(action, ok, message);
          Changed();
        },
        Qt::QueuedConnection);
    return true;
  }

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
      emit actionFinished(action, ok, QString());
      Changed();
    });
  });
  return true;
}

QVariantMap SettingsModel::menu(const QString& tag) const
{
  QVariantMap menu = m_menus.value(tag).toObject().toVariantMap();
  QVariantList items;
  for (const QVariant& item : menu.value(QStringLiteral("items")).toList())
  {
    const QVariantMap map = item.toMap();
    if (map.value(QStringLiteral("type")).toString() != QStringLiteral("logTypes"))
    {
      items.append(item);
      continue;
    }
    // NativeLibrary.GetLogTypeNames: the full name as the title, the short name as the key.
    if (auto* const log_manager = LogManager::GetInstance())
    {
      for (const auto& type : log_manager->GetLogTypes())
      {
        items.append(QVariantMap{
            {QStringLiteral("type"), QStringLiteral("switch")},
            {QStringLiteral("setting"),
             QStringLiteral("LogType.%1").arg(QString::fromStdString(type.m_short_name))},
            {QStringLiteral("title"), QString::fromStdString(type.m_full_name)},
            {QStringLiteral("description"), QString()},
            {QStringLiteral("inverted"), false},
            {QStringLiteral("globalOnly"), true},
        });
      }
    }
  }
  menu[QStringLiteral("items")] = items;
  return menu;
}

QVariantMap SettingsModel::choices(const QString& kind, const QVariantList& entries,
                                   const QVariantList& values) const
{
  QVariantList out_entries, out_values;
  if (kind == QStringLiteral("postProcessingShaders"))
  {
    // SettingsFragmentPresenter.addEnhanceSettings: the anaglyph shaders in anaglyph stereo mode.
    out_entries = entries;
    out_values = values;
    const auto shaders = Config::Get(Config::GFX_STEREO_MODE) == StereoMode::Anaglyph ?
                             VideoCommon::PostProcessing::GetAnaglyphShaderList() :
                             VideoCommon::PostProcessing::GetShaderList();
    for (const std::string& shader : shaders)
    {
      out_entries.append(QString::fromStdString(shader));
      out_values.append(QString::fromStdString(shader));
    }
  }
  else if (kind == QStringLiteral("logVerbosity"))
  {
    const int max = static_cast<int>(Common::Log::MAX_EFFECTIVE_LOGLEVEL);
    for (qsizetype i = 0; i < entries.size() && i < values.size(); ++i)
    {
      if (values[i].toInt() <= max)
      {
        out_entries.append(entries[i]);
        out_values.append(values[i]);
      }
    }
  }
  else
  {
    out_entries = entries;
    out_values = values;
  }
  return {{QStringLiteral("entries"), out_entries}, {QStringLiteral("values"), out_values}};
}

bool SettingsModel::lockedWhileRunning(const QString& name) const
{
  return m_locked_while_running.contains(name.toStdString());
}

bool SettingsModel::IsLockedNow(const std::string& name) const
{
  return m_locked_while_running.contains(name) &&
         !Core::IsUninitialized(Core::System::GetInstance());
}

QVariant SettingsModel::value(const QString& name) const
{
  const auto it = m_bindings.find(name.toStdString());
  return it != m_bindings.end() ? it->second.get() : QVariant();
}

void SettingsModel::setValue(const QString& name, const QVariant& value)
{
  const auto it = m_bindings.find(name.toStdString());
  if (it == m_bindings.end() || IsLockedNow(it->first))
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
  emit valueChanged(name, it->second.get());
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
  if (it == m_bindings.end() || m_game_id.isEmpty() || IsLockedNow(it->first))
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

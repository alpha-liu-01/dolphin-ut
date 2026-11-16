// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/SettingsModel.h"

#include <memory>
#include <type_traits>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include "Common/CommonTypes.h"
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

// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <thread>

#include <QJsonObject>
#include <QObject>
#include <QVariant>

#include "Common/Config/Config.h"

// The settings (stage 3.6, docs/ut-port/stage3.6-settings.md): the Android app's menus, generated
// from its sources (settings.json, ut/tools/gen-settings.py), over Dolphin's own Config::Info objects
// (SettingsTable.inc). Global settings go to the base layer, as the Android app's
// LAYER_BASE_OR_CURRENT; game settings to the game's own layer (GameSettings/<ID>.ini), as its
// NativeConfig with LAYER_LOCAL_GAME.
class SettingsModel : public QObject
{
  Q_OBJECT
  // Bumped on every change, so QML bindings that read values re-evaluate.
  Q_PROPERTY(int revision READ GetRevision NOTIFY changed)
  Q_PROPERTY(QString gameId READ GetGameId NOTIFY gameChanged)
  Q_PROPERTY(bool busy READ IsBusy NOTIFY busyChanged)

public:
  // game: the game's layer (LocalGame); otherwise the base layer, or the current run's when a
  // running game overrides the setting (the Android app's LAYER_BASE_OR_CURRENT).
  struct Binding
  {
    std::function<QVariant()> get;
    std::function<void(bool game, const QVariant&)> set;
    std::function<bool()> in_game_layer;
    std::function<void(bool game)> clear;
  };

  explicit SettingsModel(QObject* parent = nullptr);
  ~SettingsModel() override;

  int GetRevision() const { return m_revision; }
  QString GetGameId() const { return m_game_id; }

  // {title, items}: the menu's items as generated (types: header, submenu, page, switch, choice,
  // slider, text, file, folder, action, datetime). The log menu's "logTypes" item becomes a switch
  // per Dolphin log type (stage 6.2), as the Android app's LogSwitchSetting items.
  Q_INVOKABLE QVariantMap menu(const QString& tag) const;
  // A choice whose list the Android app builds at run time (stage 6.2; the item's "dynamic"):
  // {entries, values}, from the item's own entries and values.
  // - "postProcessingShaders": "Off", then Dolphin's shaders (the anaglyph ones in anaglyph mode);
  // - "logVerbosity": the levels this build can log (up to MAX_EFFECTIVE_LOGLEVEL).
  Q_INVOKABLE QVariantMap choices(const QString& kind, const QVariantList& entries,
                                  const QVariantList& values) const;
  Q_INVOKABLE QVariant value(const QString& name) const;
  // Whether the setting cannot change while a game runs (stage 6.1): the Android app's
  // NOT_RUNTIME_EDITABLE lists and the SYSCONF settings (settings.json, "lockedWhileRunning").
  // setValue and clear refuse such a setting while a game runs.
  Q_INVOKABLE bool lockedWhileRunning(const QString& name) const;
  Q_INVOKABLE void setValue(const QString& name, const QVariant& value);
  // Game settings: whether the game's layer sets it, and removing it there ("Long press a setting
  // to clear it").
  Q_INVOKABLE bool overridden(const QString& name) const;
  Q_INVOKABLE void clear(const QString& name);

  // Edit a game's settings from now on (its GameSettings/<ID>.ini). Not while a game runs.
  Q_INVOKABLE bool openGame(const QString& game_id, int revision);
  Q_INVOKABLE void closeGame();
  // The Android app's "Clear Game Settings": deletes the user's GameSettings/<ID>.ini.
  Q_INVOKABLE bool clearGameSettings(const QString& game_id);

  // The menus' actions: "sd_pack", "sd_unpack" (stage 3.7: the Wii SD card image and its sync
  // folder); "log_enable_all", "log_disable_all", "log_clear", "jit_wipe_profiling",
  // "jit_write_block_log" (stage 6.2: the log and debug menus). On a worker thread;
  // actionFinished reports the result, with a message to show (or "").
  Q_INVOKABLE bool runAction(const QString& action);
  bool IsBusy() const { return m_busy; }

signals:
  void changed();
  void gameChanged();
  void busyChanged();
  void actionFinished(const QString& action, bool ok, const QString& message);
  // A setting set through setValue (global or for a game), with its new value as read back.
  void valueChanged(const QString& name, const QVariant& value);

private:
  void Changed();

  bool IsLockedNow(const std::string& name) const;

  QJsonObject m_menus;
  std::set<std::string> m_locked_while_running;
  std::map<std::string, Binding> m_bindings;
  QString m_game_id;
  int m_revision = 0;
  bool m_busy = false;
  std::thread m_action_thread;
};

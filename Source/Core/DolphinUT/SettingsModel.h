// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <map>
#include <string>

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

  int GetRevision() const { return m_revision; }
  QString GetGameId() const { return m_game_id; }

  // {title, items}: the menu's items as generated (types: header, submenu, page, switch, choice,
  // slider, text).
  Q_INVOKABLE QVariantMap menu(const QString& tag) const;
  Q_INVOKABLE QVariant value(const QString& name) const;
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

signals:
  void changed();
  void gameChanged();

private:
  void Changed();

  QJsonObject m_menus;
  std::map<std::string, Binding> m_bindings;
  QString m_game_id;
  int m_revision = 0;
};

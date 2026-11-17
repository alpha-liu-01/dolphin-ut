// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <QObject>
#include <QString>
#include <QVariantList>

#include "Core/ActionReplay.h"
#include "Core/GeckoCode.h"
#include "Core/PatchEngine.h"

class GraphicsModGroupConfig;
struct GraphicsModConfig;

// A game's cheats (stage 6.4, docs/ut-port/stage6.4-game-tools.md), as the Android app's Edit
// Cheats (features/cheats, Source/Android/jni/Cheats): patches, AR codes and Gecko codes from the
// game's default and user INIs, and the game's graphics mods. Codes the user adds go to
// GameSettings/<ID>.ini, as there; each change is saved at once (Android saves when the screen
// stops). Only while no game runs: the codes are read at boot.
class Cheats : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString gameId READ GetGameId NOTIFY changed)
  // {type ("patch", "ar", "gecko", "mod"), index, name, creator, notes, code, enabled, userDefined},
  // in the Android app's order: patches, AR codes, Gecko codes, graphics mods.
  Q_PROPERTY(QVariantList items READ GetItems NOTIFY changed)
  Q_PROPERTY(bool downloading READ IsDownloading NOTIFY downloadingChanged)

public:
  explicit Cheats(QObject* parent = nullptr);
  ~Cheats() override;

  QString GetGameId() const { return m_game_id; }
  QVariantList GetItems() const;
  bool IsDownloading() const { return m_downloading; }

  Q_INVOKABLE bool load(const QString& game_id, int revision, const QString& gametdb_id);
  Q_INVOKABLE void setEnabled(const QString& type, int index, bool enabled);
  // A new code ("patch", "ar", "gecko"), or a user-defined one changed: "" when done, or the
  // Android app's error ("Name can't be empty", "Error on line 3", …).
  Q_INVOKABLE QString add(const QString& type, const QString& name, const QString& creator,
                          const QString& notes, const QString& code);
  Q_INVOKABLE QString edit(const QString& type, int index, const QString& name,
                           const QString& creator, const QString& notes, const QString& code);
  Q_INVOKABLE void remove(const QString& type, int index);
  // Download Gecko Codes (Gecko::DownloadCodes by GameTDB ID); downloadFinished reports it.
  Q_INVOKABLE bool downloadGecko();

signals:
  void changed();
  void downloadingChanged();
  // downloaded < 0: the download failed.
  void downloadFinished(int downloaded, int added);

private:
  QString Apply(const QString& type, int index, const QString& name, const QString& creator,
                const QString& notes, const QString& code);
  void Save(const QString& type);

  QString m_game_id;
  int m_revision = 0;
  std::string m_gametdb_id;
  std::vector<PatchEngine::Patch> m_patches;
  std::vector<ActionReplay::ARCode> m_ar_codes;
  std::vector<Gecko::GeckoCode> m_gecko_codes;
  std::unique_ptr<GraphicsModGroupConfig> m_mod_group;
  std::vector<GraphicsModConfig*> m_mods;
  bool m_downloading = false;
  std::thread m_download_thread;
};

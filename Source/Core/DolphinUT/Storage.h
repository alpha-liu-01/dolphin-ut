// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <thread>

#include <QObject>
#include <QString>
#include <QVariant>

// Where the port keeps its files (stage 3.7, docs/ut-port/stage3.7-storage.md), so that it works
// inside click confinement:
// - the user folder (settings, saves, states, caches): the app's own data folder,
//   ~/.local/share/<package>/, when Lomiri started the app (APP_ID is set); `-u` overrides it;
// - games: ~/Documents/Dolphin/Games, which MTP shows on a computer and the document_files policy
//   group lets a click read (also on a microSD card, <card>/Documents/...);
// - screenshots: ~/Pictures/Dolphin/<game ID>/, which the Gallery shows (picture_files).
class Storage : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString appId READ GetAppId CONSTANT)
  Q_PROPERTY(QString userFolder READ GetUserFolder CONSTANT)
  Q_PROPERTY(QString gamesFolder READ GetGamesFolder CONSTANT)
  Q_PROPERTY(QString screenshotsFolder READ GetScreenshotsFolder CONSTANT)
  Q_PROPERTY(bool importing READ IsImporting NOTIFY importingChanged)

public:
  // The click package name (APP_ID is "<package>_<app>_<version>", or a desktop file name for a
  // non-click app), or empty when the app was not started by Lomiri (adb, scripts).
  static QString PackageName();
  // Qt's names, which place QSettings (~/.config/<organization>/<application>.conf) and Qt's
  // caches: a click may only write ~/.config/<package>/ and ~/.cache/<package>/.
  static void ConfigureApplication();
  // The folder for UICommon::SetUserDirectory: the command line's, else the app's data folder, else
  // empty (Dolphin's default).
  static std::string UserFolder(const std::string& from_command_line);
  // After UICommon::Init: creates the shared folders, points screenshots at ~/Pictures/Dolphin and
  // adds the games folder to the library once.
  static void SetUp();
  // Screenshots to ~/Pictures/Dolphin (again after Dolphin's user paths are rebuilt).
  static void ApplyScreenshotsFolder();

  static QString GamesFolderPath();
  static QString ScreenshotsFolderPath();
  // Whether the library would list the file (Dolphin's game extensions).
  static bool IsGameFile(const QString& path);

  explicit Storage(QObject* parent = nullptr);
  ~Storage() override;

  QString GetAppId() const;
  QString GetUserFolder() const;
  QString GetGamesFolder() const { return GamesFolderPath(); }
  QString GetScreenshotsFolder() const { return ScreenshotsFolderPath(); }
  bool IsImporting() const { return m_importing; }

  // Places for the file and folder pickers: [{name, path, icon}]: the games folder, Documents,
  // Downloads, home, and each mounted removable card.
  Q_INVOKABLE QVariantList places() const;
  // Files handed over by content-hub: game files are moved into the games folder, others are left
  // in content-hub's store (which it clears). content-hub has already hard-linked the file into the
  // app's cache (~/.cache/<package>/HubIncoming/) or copied it there from another file system, so a
  // rename is enough; then a hard link, then a copy (confinement may forbid the first two). On a
  // worker thread, as a copy of a disc image takes a while; importFinished reports
  // {imported: [names], skipped: [{name, reason}]}.
  Q_INVOKABLE bool importFiles(const QVariantList& urls);
  Q_INVOKABLE bool isGameFile(const QString& path) const { return IsGameFile(path); }
  Q_INVOKABLE bool exists(const QString& path) const;

signals:
  void importingChanged();
  void importFinished(const QVariantMap& result);

private:
  static QVariantMap ImportFiles(const QStringList& paths);

  bool m_importing = false;
  // Joined before the next import and when the app quits, so a quit waits for a copy to end.
  std::thread m_import_thread;
};

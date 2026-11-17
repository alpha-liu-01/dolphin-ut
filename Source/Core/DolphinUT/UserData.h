// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <functional>
#include <thread>

#include <QObject>
#include <QString>

// The user data's backup and restore (stage 6.5, docs/ut-port/stage6.5-user-data.md), as the
// Android app's UserDataActivity: Export User Data (the whole user folder as a zip), Import User
// Data (a zip with Config/Dolphin.ini replaces the user folder, then the app closes), Reset Dolphin
// Settings (SConfig::ResetAllSettings); and the port's file copy into the user folder (save states,
// memory cards from elsewhere). The zips are read and written with minizip-ng, on a worker thread,
// only while no game runs.
class UserData : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool busy READ IsBusy NOTIFY busyChanged)
  Q_PROPERTY(QString task READ GetTask NOTIFY busyChanged)
  // Files done and in all (0: not known yet), and the file being worked on.
  Q_PROPERTY(int progress READ GetProgress NOTIFY progressChanged)
  Q_PROPERTY(int progressTotal READ GetProgressTotal NOTIFY progressChanged)
  Q_PROPERTY(QString progressText READ GetProgressText NOTIFY progressChanged)
  // Where exports go by default: Documents/Dolphin/Backups (visible over USB and in the File Manager).
  Q_PROPERTY(QString backupFolder READ GetBackupFolder CONSTANT)

public:
  explicit UserData(QObject* parent = nullptr);
  ~UserData() override;

  bool IsBusy() const { return m_busy; }
  QString GetTask() const { return m_task; }
  int GetProgress() const { return m_progress; }
  int GetProgressTotal() const { return m_progress_total; }
  QString GetProgressText() const { return m_progress_text; }
  QString GetBackupFolder() const;

  // A new backup's path in the backup folder: dolphin-emu-<date>-<time>.zip.
  Q_INVOKABLE QString newBackupPath() const;
  Q_INVOKABLE bool exportTo(const QString& zip_path);
  // Whether the file is a Dolphin user data backup (has Config/Dolphin.ini), as
  // isDolphinUserDataBackup; "" if so, else the reason.
  Q_INVOKABLE QString checkBackup(const QString& zip_path) const;
  Q_INVOKABLE bool importFrom(const QString& zip_path);
  Q_INVOKABLE void cancel() { m_cancel = true; }
  // After an import: the app ends without saving anything, so the imported settings stay
  // (Android: exitProcess(0)).
  Q_INVOKABLE void quitNow();

  // Reset Dolphin Settings, then the port's own defaults again (PortDefaults).
  Q_INVOKABLE bool resetSettings();

  // Copies a file into a folder of the user data (a save state or memory card from elsewhere);
  // "" when done, else the reason. An existing file is replaced only with overwrite.
  Q_INVOKABLE QString copyIn(const QString& file, const QString& folder, bool overwrite);
  // Whether path lies in the user folder.
  Q_INVOKABLE bool isInUserFolder(const QString& path) const;

signals:
  void busyChanged();
  void progressChanged();
  // task: "export" or "import"; message: the Android app's text.
  void finished(const QString& task, bool ok, bool cancelled, const QString& message);

private:
  struct Result
  {
    bool ok;
    QString message;
  };
  bool Run(const QString& task, std::function<Result()> work);
  void SetProgress(int done, int total, const QString& text);
  Result Export(const QString& zip_path);
  Result Import(const QString& zip_path);

  bool m_busy = false;
  QString m_task;
  int m_progress = 0;
  int m_progress_total = 0;
  QString m_progress_text;
  std::atomic<bool> m_cancel = false;
  std::thread m_thread;
};

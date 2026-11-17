// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <QObject>
#include <QString>

// The Wii's system actions (stage 6.3, docs/ut-port/stage6.3-wii-files.md), as the Android app's
// main menu (MainPresenter and Source/Android/jni/WiiUtils.cpp): Install WAD, Import Wii Save,
// Import BootMii NAND Backup, the online system update (which also installs the Wii Menu) and a
// game disc's system update, plus the installed Wii Menu's version for "Load Wii System Menu".
// Each runs Dolphin's own code (Core/WiiUtils, Core/HW/WiiSave, DiscIO/NANDImporter) on a worker
// thread, only while no game runs; finished() reports the result with the Android app's texts.
class WiiSystem : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool menuInstalled READ IsMenuInstalled NOTIFY menuChanged)
  // "4.3E" and the like (DiscIO::GetSysMenuVersionString), or "".
  Q_PROPERTY(QString menuVersion READ GetMenuVersion NOTIFY menuChanged)
  Q_PROPERTY(bool menuIsVWii READ IsMenuVWii NOTIFY menuChanged)
  Q_PROPERTY(bool busy READ IsBusy NOTIFY busyChanged)
  // While busy: what runs ("update", "wad", "save", "nand"), the step's text, and the progress
  // (total 0: unknown).
  Q_PROPERTY(QString task READ GetTask NOTIFY busyChanged)
  Q_PROPERTY(QString progressText READ GetProgressText NOTIFY progressChanged)
  Q_PROPERTY(int progress READ GetProgress NOTIFY progressChanged)
  Q_PROPERTY(int progressTotal READ GetProgressTotal NOTIFY progressChanged)
  Q_PROPERTY(bool cancelling READ IsCancelling NOTIFY progressChanged)

public:
  explicit WiiSystem(QObject* parent = nullptr);
  ~WiiSystem() override;

  bool IsMenuInstalled() const { return m_menu_installed; }
  QString GetMenuVersion() const { return m_menu_version; }
  bool IsMenuVWii() const { return m_menu_vwii; }
  bool IsBusy() const { return m_busy; }
  QString GetTask() const { return m_task; }
  QString GetProgressText() const { return m_progress_text; }
  int GetProgress() const { return m_progress; }
  int GetProgressTotal() const { return m_progress_total; }
  bool IsCancelling() const { return m_cancel; }

  // Reads the installed Wii Menu (WiiUtils.isSystemMenuInstalled / getSystemMenuVersion).
  Q_INVOKABLE void refresh();

  // Each returns false if it cannot start (busy, or a game runs).
  Q_INVOKABLE bool installWad(const QString& path);
  Q_INVOKABLE bool importSave(const QString& path);
  Q_INVOKABLE bool importNand(const QString& path);
  // region: "EUR", "JPN", "KOR", "USA", or "" for the installed Wii Menu's region.
  Q_INVOKABLE bool onlineUpdate(const QString& region);
  Q_INVOKABLE bool discUpdate(const QString& path);
  // The online, disc and NAND actions can be cancelled.
  Q_INVOKABLE void cancel();

  // Answers to the questions asked while an action runs.
  Q_INVOKABLE void answerOverwrite(bool overwrite);
  Q_INVOKABLE void provideKeysFile(const QString& path);  // "" gives up

signals:
  void menuChanged();
  void busyChanged();
  void progressChanged();
  // title and message as the Android app shows them; message may be "" (nothing to show).
  void finished(const QString& task, bool ok, const QString& title, const QString& message);
  // Import Wii Save: the save exists; answerOverwrite.
  void overwriteAsked();
  // Import BootMii NAND Backup: the backup has no keys appended; provideKeysFile.
  void keysFileAsked();

private:
  struct Result
  {
    bool ok;
    QString title;
    QString message;
  };

  bool Run(const QString& task, std::function<Result()> work);
  void SetProgress(const QString& text, int progress, int total);
  // Blocks the worker until the UI answers (or the app quits).
  QString AskFromWorker(void (WiiSystem::*signal)());

  bool m_menu_installed = false;
  QString m_menu_version;
  bool m_menu_vwii = false;

  bool m_busy = false;
  QString m_task;
  QString m_progress_text;
  int m_progress = 0;
  int m_progress_total = 0;
  std::atomic<bool> m_cancel = false;
  std::thread m_thread;

  std::mutex m_answer_mutex;
  std::condition_variable m_answer_cv;
  std::optional<QString> m_answer;
  bool m_quitting = false;
};

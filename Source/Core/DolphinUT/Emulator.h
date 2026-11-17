// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <memory>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "Common/CommonTypes.h"
#include "Common/HookableEvent.h"
#include "Common/MsgHandler.h"
#include "Common/WindowSystemInfo.h"
#include "Core/Core.h"

class GameWindow;
class LomiriIntegration;
class QWindow;
class TouchOverlay;

// The bridge between QML and Dolphin's core (stage 3.0 shell; grows into 3.1/3.2).
class Emulator : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString state READ state NOTIFY stateChanged)
  Q_PROPERTY(QString log READ log NOTIFY logChanged)
  Q_PROPERTY(bool hideLauncherWhilePlaying MEMBER m_hide_launcher NOTIFY settingsChanged)
  Q_PROPERTY(bool pauseWhenInactive MEMBER m_pause_when_inactive NOTIFY settingsChanged)
  // The library window is shown over a running game (stage 6.1: the in-game menu's Settings, or a
  // question from Dolphin); the game waits paused behind it.
  Q_PROPERTY(bool overGame READ IsOverGame NOTIFY overGameChanged)

public:
  explicit Emulator(LomiriIntegration* lomiri, QObject* parent = nullptr);
  ~Emulator() override;

  void SetLauncherWindow(QWindow* window);
  void Shutdown();

  QString state() const { return m_state; }
  QString log() const { return m_log; }

  // backend: "Vulkan" or "OGL" for this run only; empty for the configured one.
  Q_INVOKABLE void start(const QString& path, const QString& backend = {})
  {
    Boot(path, backend, false);
  }
  // Start with Riivolution Patches (stage 6.4): the choices saved by the Riivolution page, as the
  // Android app's Run with riivolution (AddRiivolutionPatches).
  Q_INVOKABLE void startWithRiivolution(const QString& path) { Boot(path, {}, true); }
  // Load Wii System Menu (stage 6.3): the installed Wii Menu, booted from the NAND as the Android
  // app's NativeLibrary.RunSystemMenu (BootParameters::NANDTitle).
  Q_INVOKABLE void startSystemMenu() { start(QString::fromLatin1(SYSTEM_MENU_PATH)); }
  // start()'s path for the Wii Menu, so the OpenGL ES restart after a Vulkan failure works for it.
  static constexpr const char* SYSTEM_MENU_PATH = "dolphin-ut:wii-system-menu";
  Q_INVOKABLE void stop();
  Q_INVOKABLE void appendLog(const QString& text);
  // Pauses a running game or resumes a paused one.
  Q_INVOKABLE void togglePause();
  // The in-game menu's Settings (stage 6.1): the library window over the paused game, with the
  // settings. returnToGame closes them (also when their page goes) and resumes the game.
  Q_INVOKABLE void openSettingsOverGame();
  // Stage 6.8: another page over the paused game, as the settings: "skylanders" or "infinity"
  // (FiguresPage.qml). returnToGame closes it as it closes the settings.
  Q_INVOKABLE void openPageOverGame(const QString& page);
  Q_INVOKABLE void returnToGame();
  // For tests over adb: the newest open question (0: none).
  int LatestQuestionId() const { return m_answers.empty() ? 0 : m_answers.rbegin()->first; }
  // The answer to questionAsked: 0 OK or No, 1 Yes, 2 Ignore for this session.
  Q_INVOKABLE void answerQuestion(int id, int button);

  bool IsOverGame() const { return m_over_game > 0; }


  // Load this slot once the game runs (0: none).
  void SetBootStateSlot(int slot) { m_boot_state_slot = slot; }

  // Safe to call from any thread (core callbacks, the video thread).
  static void LogFromAnyThread(const QString& text);
  static void RequestStopFromAnyThread();
  static bool GameWindowHasFocus();
  // From the message alert handler (any thread). Notes "Failed to initialize video backend!" during
  // a boot (3.8) and VK_ERROR_DEVICE_LOST during play (stage 4), so the game can be restarted on
  // OpenGL ES. Returns true for an alert to drop: the repeats after a lost device (thousands).
  static bool NoteAlert(const char* text);
  // Dolphin's alerts and questions (stage 6.1), from any thread: a dialog in the library window,
  // shown over the game if one runs, as DolphinQt's and the Android app's handlers do. The calling
  // thread waits for the answer. Returns Yes for a question; true otherwise.
  static bool AskFromAnyThread(const char* caption, const char* text, bool yes_no,
                               Common::MsgType style);

signals:
  void stateChanged();
  // The in-game menu's Settings: the library window, shown over the paused game, opens them.
  void settingsRequested();
  void pageOverGameRequested(const QString& page);
  // The settings over the game are closed: their pages go.
  void settingsClosed();
  void overGameChanged();
  // A dialog for Dolphin's alert ID; answered with answerQuestion.
  void questionAsked(int id, const QString& caption, const QString& text, bool yesNo, bool warning);
  void questionAnswered(int id);
  void logChanged();
  void settingsChanged();

private:
  void OnCoreStateChanged(Core::State state);
  void OnApplicationStateChanged(Qt::ApplicationState state);
  void OnGameWindowSizeChanged(int width, int height, float scale);
  void FinishStop();
  void Boot(const QString& path, const QString& backend, bool riivolution);
  // Hotkeys (stage 6.2) only while the game is in front: not over the game or with the app in the
  // background, as the Android app's Hotkeys.setEnabled in onResume and onPause.
  void UpdateHotkeysEnabled();
  // Show the library window over the game, pausing it (counted: settings and questions can
  // overlap); the last End hides the window again and resumes the game if it was running.
  // pause: false for Dolphin's questions, whose asking thread (CPU or GPU) is blocked: pausing the
  // core from here could wait for it.
  void BeginOverGame(const QString& reason, bool pause = true);
  void EndOverGame(const QString& reason);
  enum class AskingThread
  {
    UI,
    CPU,
    GPU,
    Other,
  };
  bool Ask(const QString& caption, const QString& text, bool yes_no, Common::MsgType style,
           AskingThread thread);
  WindowSystemInfo MakeWindowSystemInfo(QWindow* window) const;

  LomiriIntegration* m_lomiri;
  QPointer<QWindow> m_launcher;
  GameWindow* m_game_window = nullptr;
  std::unique_ptr<TouchOverlay> m_overlay;
  QTimer m_host_jobs;
  QTimer m_perf_log;
  QTimer m_paused_redraw;
  int m_cpu_boost = 0;  // the uclamp_min applied while running
  Common::EventHook m_state_hook;
  Core::State m_core_state = Core::State::Uninitialized;
  bool m_paused_by_app_state = false;
  bool m_hide_launcher = true;
  bool m_pause_when_inactive = true;
  bool m_controllers_initialized = false;
  int m_boot_state_slot = 0;
  // The game being started, and whether the core reached Running or Paused (a failed boot goes from
  // Starting straight back to Uninitialized).
  QString m_boot_path;
  bool m_boot_riivolution = false;
  bool m_boot_reached_running = false;
  // This run is the OpenGL ES retry after Vulkan failed: the note to show once it runs.
  QString m_fallback_note;
  // Requests for a normal library window since it was shown again (Lomiri may answer with
  // fullscreen again; at most 3).
  int m_library_normal_requests = 0;
  // BeginOverGame calls not yet ended, whether the in-game settings are one of them, and whether
  // the game ran before the first (to resume it after the last).
  int m_over_game = 0;
  bool m_settings_over_game = false;
  bool m_resume_after_over_game = false;
  // Open questions (nested if two threads ask): id -> answer, -1 until answered.
  std::map<int, int> m_answers;
  int m_next_question_id = 1;
  QString m_state = QStringLiteral("idle");
  QString m_log;
};

// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "Common/CommonTypes.h"
#include "Common/HookableEvent.h"
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

public:
  explicit Emulator(LomiriIntegration* lomiri, QObject* parent = nullptr);
  ~Emulator() override;

  void SetLauncherWindow(QWindow* window);
  void Shutdown();

  QString state() const { return m_state; }
  QString log() const { return m_log; }

  // backend: "Vulkan" or "OGL" for this run only; empty for the configured one.
  Q_INVOKABLE void start(const QString& path, const QString& backend = {});
  Q_INVOKABLE void stop();
  Q_INVOKABLE void appendLog(const QString& text);

  // The overlay's Save / Load use this state slot (Dolphin's usual StateSaves/<game id>.s01).
  static constexpr u32 TEST_STATE_SLOT = 1;
  // Load this slot once the game runs (0: none).
  void SetBootStateSlot(int slot) { m_boot_state_slot = slot; }

  // Safe to call from any thread (core callbacks, the video thread).
  static void LogFromAnyThread(const QString& text);
  static void RequestStopFromAnyThread();
  static bool GameWindowHasFocus();

signals:
  void stateChanged();
  void logChanged();
  void settingsChanged();

private:
  void OnCoreStateChanged(Core::State state);
  void OnApplicationStateChanged(Qt::ApplicationState state);
  void OnGameWindowSizeChanged(int width, int height, float scale);
  void FinishStop();
  WindowSystemInfo MakeWindowSystemInfo(QWindow* window) const;

  LomiriIntegration* m_lomiri;
  QPointer<QWindow> m_launcher;
  GameWindow* m_game_window = nullptr;
  std::unique_ptr<TouchOverlay> m_overlay;
  QTimer m_host_jobs;
  QTimer m_perf_log;
  Common::EventHook m_state_hook;
  Core::State m_core_state = Core::State::Uninitialized;
  bool m_paused_by_app_state = false;
  bool m_hide_launcher = true;
  bool m_pause_when_inactive = true;
  bool m_controllers_initialized = false;
  int m_boot_state_slot = 0;
  QString m_state = QStringLiteral("idle");
  QString m_log;
};

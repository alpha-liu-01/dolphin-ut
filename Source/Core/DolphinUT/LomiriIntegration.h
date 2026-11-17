// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QString>

class QWindow;

// Ubuntu Touch specifics: keeping the display on (repowerd's com.canonical.Unity.Screen, which needs
// the keep-display-on AppArmor policy group in a click), orientation, and vibration (hfd-service's
// com.lomiri.hfd.Vibrator; the phone has no Qt 6 haptics plugin).
class LomiriIntegration : public QObject
{
  Q_OBJECT
  // Stage 6.8: whether restart() can start the app again (it was started by Lomiri, with an app ID).
  Q_PROPERTY(bool canRestart READ CanRestart CONSTANT)

public:
  explicit LomiriIntegration(QObject* parent = nullptr);
  ~LomiriIntegration() override;

  void SetKeepDisplayOn(bool keep_on);
  void PreferLandscape(QWindow* window);

  // Lomiri's UI scale (GRID_UNIT_PX / 8), what Qt's device pixel ratio was before 3.1 dropped
  // QT_SCALE_FACTOR. Dolphin's own on-screen UI (statistics, messages) is sized by it.
  static float GridUnitScale();

  // Asynchronous; failures are reported through message().
  Q_INVOKABLE void vibrate(int duration_ms);

  // Stage 6.8: close the app and start it again (a new GPU driver, imported user data). A helper
  // outside the app's own systemd unit (systemd-run --user) waits for this process to end, then
  // asks Lomiri to start the app (lomiri-app-launch APP_ID); Lomiri only starts a single-instance
  // app that is not running. save false ends at once without saving (after Import User Data).
  bool CanRestart() const;
  Q_INVOKABLE bool restart(bool save);

signals:
  void message(const QString& text);

private:
  int m_display_on_cookie = -1;
};

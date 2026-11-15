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

public:
  explicit LomiriIntegration(QObject* parent = nullptr);
  ~LomiriIntegration() override;

  void SetKeepDisplayOn(bool keep_on);
  void PreferLandscape(QWindow* window);

  // Asynchronous; failures are reported through message().
  Q_INVOKABLE void vibrate(int duration_ms);

signals:
  void message(const QString& text);

private:
  int m_display_on_cookie = -1;
};

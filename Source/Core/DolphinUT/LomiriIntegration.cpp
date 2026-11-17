// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/LomiriIntegration.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>

#include <cstdlib>
#include <QScreen>
#include <QWindow>

namespace
{
QDBusInterface ScreenInterface()
{
  return QDBusInterface(QStringLiteral("com.canonical.Unity.Screen"),
                        QStringLiteral("/com/canonical/Unity/Screen"),
                        QStringLiteral("com.canonical.Unity.Screen"),
                        QDBusConnection::systemBus());
}

QString OrientationName(Qt::ScreenOrientation orientation)
{
  switch (orientation)
  {
  case Qt::PortraitOrientation:
    return QStringLiteral("portrait");
  case Qt::LandscapeOrientation:
    return QStringLiteral("landscape");
  case Qt::InvertedPortraitOrientation:
    return QStringLiteral("inverted portrait");
  case Qt::InvertedLandscapeOrientation:
    return QStringLiteral("inverted landscape");
  default:
    return QStringLiteral("primary");
  }
}
}  // namespace

LomiriIntegration::LomiriIntegration(QObject* parent) : QObject(parent)
{
}

LomiriIntegration::~LomiriIntegration()
{
  SetKeepDisplayOn(false);
}

void LomiriIntegration::SetKeepDisplayOn(bool keep_on)
{
  if (keep_on && m_display_on_cookie < 0)
  {
    QDBusInterface screen = ScreenInterface();
    const QDBusReply<int> reply = screen.call(QStringLiteral("keepDisplayOn"));
    if (reply.isValid())
    {
      m_display_on_cookie = reply.value();
      emit message(QStringLiteral("keepDisplayOn: cookie %1").arg(m_display_on_cookie));
    }
    else
    {
      emit message(QStringLiteral("keepDisplayOn failed: %1").arg(reply.error().message()));
    }
  }
  else if (!keep_on && m_display_on_cookie >= 0)
  {
    QDBusInterface screen = ScreenInterface();
    const QDBusMessage reply =
        screen.call(QStringLiteral("removeDisplayOnRequest"), m_display_on_cookie);
    emit message(QStringLiteral("removeDisplayOnRequest(%1): %2")
                     .arg(m_display_on_cookie)
                     .arg(reply.type() == QDBusMessage::ErrorMessage ? reply.errorMessage() :
                                                                       QStringLiteral("ok")));
    m_display_on_cookie = -1;
  }
}

void LomiriIntegration::vibrate(int duration_ms)
{
  // vibrate is overloaded (no argument, or an int); the message's "i" argument picks the second.
  QDBusMessage call = QDBusMessage::createMethodCall(
      QStringLiteral("com.lomiri.hfd"), QStringLiteral("/com/lomiri/hfd"),
      QStringLiteral("com.lomiri.hfd.Vibrator"), QStringLiteral("vibrate"));
  call << duration_ms;
  auto* watcher =
      new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this, duration_ms](QDBusPendingCallWatcher* finished) {
            const QDBusPendingReply<> reply = *finished;
            if (reply.isError())
              emit message(QStringLiteral("vibrate failed: %1").arg(reply.error().message()));
            else
              emit message(QStringLiteral("vibrate(%1 ms): ok").arg(duration_ms));
            finished->deleteLater();
          });
}

float LomiriIntegration::GridUnitScale()
{
  // Lomiri's scale: 1 grid unit is 8 px at scale 1 (GRID_UNIT_PX=21 on the Redmi Note 12 5G).
  bool ok = false;
  const float grid_unit = qEnvironmentVariable("GRID_UNIT_PX").toFloat(&ok);
  return ok && grid_unit >= 4.0f ? grid_unit / 8.0f : 1.0f;
}

void LomiriIntegration::PreferLandscape(QWindow* window)
{
  if (!window)
    return;
  window->reportContentOrientationChange(Qt::LandscapeOrientation);
  if (QScreen* screen = window->screen())
  {
    emit message(QStringLiteral("screen %1x%2, orientation %3, primary %4")
                     .arg(screen->size().width())
                     .arg(screen->size().height())
                     .arg(OrientationName(screen->orientation()))
                     .arg(OrientationName(screen->primaryOrientation())));
    connect(screen, &QScreen::orientationChanged, this, [this](Qt::ScreenOrientation orientation) {
      emit message(QStringLiteral("screen orientation: %1").arg(OrientationName(orientation)));
    });
  }
}

bool LomiriIntegration::CanRestart() const
{
  return !qEnvironmentVariable("APP_ID").isEmpty() &&
         !QStandardPaths::findExecutable(QStringLiteral("systemd-run")).isEmpty() &&
         !QStandardPaths::findExecutable(QStringLiteral("lomiri-app-launch")).isEmpty();
}

bool LomiriIntegration::restart(bool save)
{
  if (!CanRestart())
    return false;
  const QString app_id = qEnvironmentVariable("APP_ID");
  const QString pid = QString::number(QCoreApplication::applicationPid());
  // The app's own unit is stopped with the app (its processes killed), so the helper runs as a
  // unit of its own. It waits up to 30 s for this process to end.
  const QString script =
      QStringLiteral("i=0; while kill -0 %1 2>/dev/null && [ $i -lt 150 ]; do sleep 0.2; i=$((i+1)); done; "
                     "exec lomiri-app-launch \"$0\"")
          .arg(pid);
  const bool started = QProcess::startDetached(
      QStringLiteral("systemd-run"),
      {QStringLiteral("--user"), QStringLiteral("--collect"), QStringLiteral("--quiet"),
       QStringLiteral("/bin/sh"), QStringLiteral("-c"), script, app_id});
  emit message(QStringLiteral("restart: helper %1 for %2").arg(started ? "started" : "failed", app_id));
  if (!started)
    return false;
  if (save)
    QCoreApplication::quit();
  else
    std::_Exit(0);
  return true;
}


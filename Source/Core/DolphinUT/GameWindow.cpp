// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/GameWindow.h"

#include <algorithm>

#include <QExposeEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QTimer>
#include <QResizeEvent>
#include <QScreen>
#include <QTouchEvent>
#include <qpa/qplatformwindow.h>

#include "DolphinUT/Emulator.h"
#include "DolphinUT/TouchOverlay.h"

GameWindow::GameWindow()
{
  setTitle(QStringLiteral("Dolphin"));
  setFlag(Qt::WindowDoesNotAcceptFocus, false);
  // Sizes settle in steps (a rotation passes through the size without the panel for ~0.2 s), so
  // check once things are quiet.
  m_fullscreen_check.setSingleShot(true);
  m_fullscreen_check.setInterval(500);
  connect(&m_fullscreen_check, &QTimer::timeout, this, &GameWindow::CheckFullscreen);
  connect(this, &QWindow::activeChanged, this, [this] {
    if (isActive())
    {
      // Back from another app: a new situation, with its own retries.
      m_fullscreen_retries = 0;
      m_fullscreen_check.start();
    }
  });
}

bool GameWindow::event(QEvent* event)
{
  switch (event->type())
  {
  case QEvent::TouchBegin:
  case QEvent::TouchUpdate:
  case QEvent::TouchEnd:
  case QEvent::TouchCancel:
    if (m_overlay)
    {
      const auto* touch = static_cast<QTouchEvent*>(event);
      m_overlay->HandleTouch(touch->points(), QSizeF(PhysicalSize()), devicePixelRatio(),
                             event->type() == QEvent::TouchCancel);
    }
    event->accept();
    return true;
  case QEvent::Close:
    // Closing the game window (Lomiri's app spread) stops the game; the emulator then destroys the
    // window itself.
    emit CloseRequested();
    event->ignore();
    return true;
  default:
    return QWindow::event(event);
  }
}

namespace
{
bool IsBackKey(const QKeyEvent* event)
{
  return event->key() == Qt::Key_Escape || event->key() == Qt::Key_Back;
}

// As Android's onKeyLongPress for Back.
constexpr qint64 LONG_PRESS_MS = 500;
}  // namespace

void GameWindow::keyPressEvent(QKeyEvent* event)
{
  if (!IsBackKey(event))
    return QWindow::keyPressEvent(event);
  if (!event->isAutoRepeat())
    m_back_pressed.start();
  event->accept();
}

void GameWindow::keyReleaseEvent(QKeyEvent* event)
{
  if (!IsBackKey(event))
    return QWindow::keyReleaseEvent(event);
  event->accept();
  if (event->isAutoRepeat() || !m_back_pressed.isValid())
    return;
  const bool long_press = m_back_pressed.elapsed() >= LONG_PRESS_MS;
  m_back_pressed.invalidate();
  if (long_press)
    emit StopRequested();
  else
    emit MenuRequested();
}

QSize GameWindow::PhysicalSize() const
{
  if (const QPlatformWindow* platform_window = handle())
    return platform_window->geometry().size();
  return size() * devicePixelRatio();
}

// Lomiri (Mir 1.8) ignores xdg_toplevel.set_fullscreen sent before the window is first mapped, and
// the top panel then stays over the game. Map the window normally, then ask for fullscreen, as
// DolphinQt's fullscreen toggle does (docs/ut-port/stage3.2-emulation.md).
void GameWindow::exposeEvent(QExposeEvent* event)
{
  QWindow::exposeEvent(event);
  if (isExposed() && !m_fullscreen_requested)
  {
    m_fullscreen_requested = true;
    QTimer::singleShot(0, this, [this] { showFullScreen(); });
  }
}

// Lomiri managing the app (started from the drawer, not over adb) keeps window states per app, and
// does not always give the fullscreen window the whole screen: after switching back to the app,
// the game came back 2400x1017 in landscape, the size without the 63 px panel, with the panel
// hidden, so the wallpaper showed in a strip below the picture (S5). Asking for fullscreen again,
// as a rotation does, gives it the whole screen.
void GameWindow::CheckFullscreen()
{
  if (!m_fullscreen_requested || !isExposed() || !screen() ||
      QGuiApplication::applicationState() != Qt::ApplicationActive)
  {
    return;
  }
  const QSize size = PhysicalSize();
  const QSize full = screen()->size() * devicePixelRatio();
  const bool covers = std::min(size.width(), size.height()) >= std::min(full.width(), full.height()) &&
                      std::max(size.width(), size.height()) >= std::max(full.width(), full.height());
  if (covers)
  {
    m_fullscreen_retries = 0;
    return;
  }
  if (m_fullscreen_retries >= 3)
    return;
  ++m_fullscreen_retries;
  Emulator::LogFromAnyThread(QStringLiteral("game window %1x%2 does not cover the screen (%3x%4): "
                                            "asking for fullscreen again (%5)")
                                 .arg(size.width())
                                 .arg(size.height())
                                 .arg(full.width())
                                 .arg(full.height())
                                 .arg(m_fullscreen_retries));
  setWindowStates(Qt::WindowNoState);
  showFullScreen();
}

void GameWindow::SetOverlay(TouchOverlay* overlay)
{
  m_overlay = overlay;
  if (m_overlay)
    m_overlay->SetWindowSize(QSizeF(PhysicalSize()));
}

void GameWindow::resizeEvent(QResizeEvent* event)
{
  QWindow::resizeEvent(event);
  const QSize physical = PhysicalSize();
  if (m_overlay)
    m_overlay->SetWindowSize(QSizeF(physical));
  emit SizeChanged(physical.width(), physical.height(), static_cast<float>(devicePixelRatio()));
  if (m_fullscreen_requested)
    m_fullscreen_check.start();
}

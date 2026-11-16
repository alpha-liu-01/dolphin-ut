// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/GameWindow.h"

#include <QExposeEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QResizeEvent>
#include <QTouchEvent>
#include <qpa/qplatformwindow.h>

#include "DolphinUT/TouchOverlay.h"

GameWindow::GameWindow()
{
  setTitle(QStringLiteral("Dolphin"));
  setFlag(Qt::WindowDoesNotAcceptFocus, false);
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

void GameWindow::resizeEvent(QResizeEvent* event)
{
  QWindow::resizeEvent(event);
  const QSize physical = PhysicalSize();
  emit SizeChanged(physical.width(), physical.height(), static_cast<float>(devicePixelRatio()));
}

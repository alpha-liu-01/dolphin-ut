// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/GameWindow.h"

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

QSize GameWindow::PhysicalSize() const
{
  if (const QPlatformWindow* platform_window = handle())
    return platform_window->geometry().size();
  return size() * devicePixelRatio();
}

void GameWindow::resizeEvent(QResizeEvent* event)
{
  QWindow::resizeEvent(event);
  const QSize physical = PhysicalSize();
  emit SizeChanged(physical.width(), physical.height(), static_cast<float>(devicePixelRatio()));
}

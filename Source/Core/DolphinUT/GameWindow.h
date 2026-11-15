// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QWindow>

class TouchOverlay;

// The window the game is rendered into: a plain top-level QWindow, so that the game has its own
// Wayland surface (no subsurface, docs/ut-port/stage2-wayland.md). Qt never draws into it; the
// video backend presents to its wl_surface. Touches go to the overlay.
class GameWindow : public QWindow
{
  Q_OBJECT

public:
  GameWindow();

  void SetOverlay(TouchOverlay* overlay) { m_overlay = overlay; }

  // The window's size in device pixels, from the platform window. Computing it as logical size
  // times the device pixel ratio is off by a pixel or two with a fractional QT_SCALE_FACTOR.
  QSize PhysicalSize() const;

signals:
  // In physical pixels.
  void SizeChanged(int width, int height, float scale);
  void CloseRequested();

protected:
  bool event(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  TouchOverlay* m_overlay = nullptr;
};

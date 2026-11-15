// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <QList>
#include <QSizeF>

#include "Common/HookableEvent.h"
#include "InputCommon/ControllerInterface/Touch/InputOverrider.h"

class QEventPoint;

// Stage 3.0 spike, option A (docs/ut-port/stage3-roadmap.md): the touch overlay is drawn into the
// game picture by Dolphin's renderer, through the on-screen UI's ImGui frame, and touches on the
// game window drive ciface::Touch::InputOverrider like the Android overlay does. No Qt surface is
// layered over the game.
//
// Control positions are normalized to the game window (0..1); sizes are a fraction of its shorter
// side.
class TouchOverlay
{
public:
  enum class Layout
  {
    GameCube,
    // A Wii Remote with no extension, held sideways (New Super Mario Bros. Wii and the like).
    WiiRemoteSideways,
    // A Wii Remote held upright, with a Nunchuk.
    WiiRemoteNunchuk,
  };

  // sideways_option: the Wii Remote's "Sideways Wii Remote" setting. When it is on, Dolphin
  // already rotates the D-pad, so the overlay must not rotate it again.
  // Frontend actions on the overlay (not emulated controls), run on the UI thread when released.
  struct Actions
  {
    std::function<void()> exit;
    std::function<void()> save_state;
    std::function<void()> load_state;
  };

  TouchOverlay(Layout layout, bool sideways_option, Actions actions);
  ~TouchOverlay();

  TouchOverlay(const TouchOverlay&) = delete;
  TouchOverlay& operator=(const TouchOverlay&) = delete;

  // UI thread. window_size is in physical pixels.
  void HandleTouch(const QList<QEventPoint>& points, QSizeF window_size, qreal device_pixel_ratio,
                   bool cancel);

private:
  struct Control
  {
    enum class Kind
    {
      Button,
      Stick,
      Action,
    };
    Kind kind;
    std::string label;
    float x, y, radius;
    ciface::Touch::ControlID id = ciface::Touch::ControlID::NUMBER_OF_CONTROLS;
    ciface::Touch::ControlID id_y = ciface::Touch::ControlID::NUMBER_OF_CONTROLS;
    std::atomic<bool> pressed = false;
    std::atomic<float> stick_x = 0.0f;
    std::atomic<float> stick_y = 0.0f;
    std::function<void()> action;
  };

  void AddButton(const char* label, float x, float y, float radius, ciface::Touch::ControlID id);
  void AddStick(const char* label, float x, float y, float radius, ciface::Touch::ControlID id_x,
                ciface::Touch::ControlID id_y);
  void BuildGameCube();
  void BuildWiiRemoteSideways(bool sideways_option);
  void BuildWiiRemoteNunchuk();
  void AddAction(const char* label, float x, float y, std::function<void()> action);
  void AddActions();

  Control* HitTest(float x, float y, QSizeF window_size) const;
  void Press(Control* control, float x, float y, QSizeF window_size);
  void Release(Control* control);

  // Video thread, while the on-screen UI's ImGui frame is open.
  void Draw();

  Layout m_layout;
  int m_controller_index = 0;
  Actions m_actions;
  std::vector<std::unique_ptr<Control>> m_controls;
  std::map<int, Control*> m_touches;  // UI thread only

  Common::EventHook m_draw_hook;
  int m_last_drawn_frame = -1;  // video thread only
  std::chrono::nanoseconds m_draw_time{};
  int m_draw_count = 0;
};

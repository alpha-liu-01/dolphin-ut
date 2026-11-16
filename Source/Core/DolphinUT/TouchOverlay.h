// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QElapsedTimer>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include "Common/CommonTypes.h"
#include "Common/HookableEvent.h"
#include "DolphinUT/OverlayLayout.h"

class QEventPoint;
struct ImTextureData;

// The touch overlay (stage 3.3, docs/ut-port/stage3.3-overlay.md): the Android app's overlay, with
// its layouts, graphics and touch behavior (InputOverlay*.kt) and its "Overlay Controls" menu and
// edit mode (EmulationActivity), drawn into the game picture by Dolphin's renderer through the
// on-screen UI's ImGui frame (option A, stage 3.0). Touches arrive on the UI thread and drive
// ciface::Touch::InputOverrider, as the Android app does through JNI.
class TouchOverlay
{
public:
  // Frontend actions (not emulated controls), until the in-game menu (3.4) takes them over.
  struct Actions
  {
    std::function<void()> exit;
    std::function<void()> save_state;
    std::function<void()> load_state;
  };

  // wii: the game is a Wii game. screen_short_side: the screen's shorter side in physical pixels;
  // controls are sized by it, as in the Android app.
  TouchOverlay(bool wii, int screen_short_side, Actions actions);
  ~TouchOverlay();

  TouchOverlay(const TouchOverlay&) = delete;
  TouchOverlay& operator=(const TouchOverlay&) = delete;

  // UI thread.
  OverlayLayout::Type GetType() const;
  int GetControllerIndex() const;
  // Sizes in physical pixels.
  void SetWindowSize(QSizeF window_size);
  void HandleTouch(const QList<QEventPoint>& points, QSizeF window_size, qreal device_pixel_ratio,
                   bool cancel);

private:
  struct Image
  {
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<u8> rgba;
  };

  enum DpadDirection : u8
  {
    DPAD_UP = 1,
    DPAD_DOWN = 2,
    DPAD_LEFT = 4,
    DPAD_RIGHT = 8,
  };

  struct Control
  {
    OverlayLayout::ControlDef def;
    bool latching = false;
    std::array<int, 3> images{-1, -1, -1};  // into m_images
    QRectF rect;                            // physical pixels
    QRectF virtual_rect;                    // joystick: the range, moved by "relative center"
    int track = -1;
    bool pressed = false;
    u8 dpad = 0;
    float stick_x = 0.0f;
    float stick_y = 0.0f;
  };

  struct ActionButton
  {
    const char* label;
    std::function<void()> action;  // empty: opens the overlay menu
    QRectF rect;
    int track = -1;
    bool pressed = false;
  };

  struct Pointer
  {
    int mode = 0;  // 0 off, 1 follow, 2 drag
    bool recenter = false;
    ciface::Touch::ControlID double_tap_control{};
    int track = -1;
    float x = 0.0f;
    float y = 0.0f;
    float old_x = 0.0f;
    float old_y = 0.0f;
    QPointF touch_start;
    QElapsedTimer last_tap;
  };

  // The Android app's overlay menu and its dialogs (EmulationActivity), drawn with ImGui.
  enum class Menu
  {
    None,
    Main,
    Toggle,
    Latching,
    Adjust,
    Controller,
    Pointer,
    Editing,  // "Edit Layout": controls are dragged, "Done" ends it
  };

  // An ImGui mouse event to send once m_mutex is released (the ImGui lock is taken by the video
  // thread before ours, so it must not be taken while holding ours).
  struct ImGuiInput
  {
    QPointF position;
    int press;  // -1 unchanged, 0 up, 1 down
  };

  // All with m_mutex held.
  void Build();
  void Teardown();
  int AddImage(const char* name, int max_side);
  void Layout();
  void ReleaseAll();
  void Press(Control& control, QPointF position, int id);
  void Move(Control& control, QPointF position);
  void Release(Control& control);
  void UpdateDpad(Control& control, QPointF position);
  void UpdateJoystick(Control& control, QPointF position);
  void PressPointer(QPointF position, int id);
  void MovePointer(QPointF position);
  void ReleasePointer();
  void SendPointer();
  void HandleEditTouch(const QEventPoint& point, QPointF position);
  bool Portrait() const { return m_window_size.height() > m_window_size.width(); }

  // Runs a settings change on the UI thread, then rebuilds the overlay from the settings.
  void ChangeSettings(std::function<void()> change);

  // Video thread, while the on-screen UI's ImGui frame is open; m_mutex held.
  void Draw();
  void DrawMenu();
  void RetireTextures();

  const bool m_wii;
  const int m_screen_short_side;
  // Guards everything below: the UI thread changes it, the video thread draws it.
  mutable std::mutex m_mutex;

  OverlayLayout::Type m_type = OverlayLayout::Type::None;
  int m_controller_index = 0;
  float m_size_scale = 1.0f;  // (ControlScale + 50) / 100
  bool m_relative_center = true;
  u32 m_alpha = 255;

  std::vector<Image> m_images;
  std::vector<Control> m_controls;
  std::vector<ActionButton> m_actions;
  Pointer m_pointer;
  bool m_has_pointer = false;
  QSizeF m_window_size;
  QRectF m_game_rect;  // the game picture in the window (from the presenter), for the pointer

  Menu m_menu = Menu::None;
  int m_menu_touch = -1;
  int m_opacity_preview = -1;  // the opacity slider while it is dragged
  int m_scale_preview = -1;
  int m_drag_control = -1;  // edit mode
  int m_drag_touch = -1;
  QPointF m_drag_last;
  QRectF m_done_rect;
  int m_done_touch = -1;

  // Video thread: the ImGui textures of m_images, and replaced ones waiting for the backend to
  // free them.
  std::vector<std::unique_ptr<ImTextureData>> m_textures;
  std::vector<std::unique_ptr<ImTextureData>> m_retired_textures;
  bool m_images_changed = true;

  std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
  Common::EventHook m_draw_hook;
  int m_last_drawn_frame = -1;
  std::chrono::nanoseconds m_draw_time{};
  int m_draw_count = 0;
};

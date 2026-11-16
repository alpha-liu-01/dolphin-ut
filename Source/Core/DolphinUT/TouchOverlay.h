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
  // What the in-game menu (stage 3.4, the Android app's MenuFragment) does; run on the UI thread.
  struct Actions
  {
    std::string game_title;
    std::function<void()> exit;
    std::function<void()> toggle_pause;
    std::function<void()> screenshot;
    std::function<void(int slot)> save_state;  // the Android app's slots: 0–5, quick save 9
    std::function<void(int slot)> load_state;
    std::function<void()> refresh_wiimotes;
    std::function<void(const std::string& path)> change_disc;
    std::function<void()> reset;
  };

  // wii: the game is a Wii game. screen_short_side: the screen's shorter side in physical pixels;
  // controls are sized by it, as in the Android app.
  TouchOverlay(bool wii, int screen_short_side, Actions actions);
  ~TouchOverlay();

  TouchOverlay(const TouchOverlay&) = delete;
  TouchOverlay& operator=(const TouchOverlay&) = delete;

  // UI thread.
  // Opens or closes the in-game menu (Back / Escape, as in the Android app).
  void ToggleMenu();
  // While the game is paused, nothing is presented; a menu that is open needs redraws
  // (Emulator::RequestPausedRedraw).
  bool WantsPausedRedraw() const;
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

  // The button that opens the in-game menu (top centre; the Android app uses Back).
  struct MenuButton
  {
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
    // The in-game menu (MenuFragment) and its pages.
    Game,
    SaveState,
    LoadState,
    ChangeDisc,
    // Overlay Controls (3.3).
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
  // Runs a menu action on the UI thread.
  void RunOnUIThread(std::function<void()> action);

  // Video thread, while the on-screen UI's ImGui frame is open; m_mutex held.
  void Draw();
  void DrawMenu();
  void DrawGameMenu();
  void OpenPage(Menu page);
  void OpenTestPage();
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
  Actions m_actions;
  MenuButton m_menu_button;
  Pointer m_pointer;
  bool m_has_pointer = false;
  QSizeF m_window_size;
  QRectF m_game_rect;  // the game picture in the window (from the presenter), for the pointer

  Menu m_menu = Menu::None;
  std::vector<std::string> m_slot_labels;  // filled when a save/load page opens
  std::vector<std::string> m_discs;        // filled when Change Disc opens
  float m_game_panel_width = 0.0f;         // the in-game menu's panel, as last drawn
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
  bool m_test_page_checked = false;

  std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
  Common::EventHook m_draw_hook;
  int m_last_drawn_frame = -1;
  std::chrono::nanoseconds m_draw_time{};
  int m_draw_count = 0;
};

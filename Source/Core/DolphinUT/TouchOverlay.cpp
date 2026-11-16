// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/TouchOverlay.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <numbers>
#include <string_view>

#include <fmt/format.h>

#include <QCoreApplication>
#include <QEventPoint>
#include <QImage>
#include <QMetaObject>
#include <QString>
#include <QTimer>

#include <imgui.h>
#include <imgui_internal.h>

#include "Common/FileUtil.h"
#include "Common/StringUtil.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/UISettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/State.h"
#include "Core/System.h"
#include "DolphinUT/Emulator.h"
#include "UICommon/GameFileCache.h"
#include "VideoCommon/OnScreenDisplay.h"
#include "InputCommon/ControllerInterface/Touch/InputOverrider.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/VideoEvents.h"

using ciface::Touch::ControlID;
using Kind = OverlayLayout::ControlDef::Kind;
using OverlayLayout::Type;

namespace
{
// The Android app's ButtonType numbers for MAIN_DOUBLE_TAP_BUTTON (NativeLibrary.ButtonType).
constexpr int BUTTON_TYPE_WIIMOTE_A = 100;
constexpr int BUTTON_TYPE_WIIMOTE_B = 101;
constexpr int BUTTON_TYPE_WIIMOTE_2 = 106;
constexpr int BUTTON_TYPE_CLASSIC_A = 300;

// InputOverlayPointer: a second tap within 300 ms presses the double-tap button for 50 ms.
constexpr qint64 DOUBLE_TAP_MS = 300;
constexpr int DOUBLE_TAP_PRESS_MS = 50;

ImVec2 ToImVec2(QPointF point)
{
  return {static_cast<float>(point.x()), static_cast<float>(point.y())};
}
}  // namespace

TouchOverlay::TouchOverlay(bool wii, int screen_short_side, Actions actions)
    : m_wii(wii), m_screen_short_side(screen_short_side), m_actions(std::move(actions))
{
  {
    std::lock_guard lock(m_mutex);
    Build();
  }
  m_draw_hook = GetVideoEvents().before_present_event.Register([this](PresentInfo&) { Draw(); });
}

void TouchOverlay::OpenTestPage()
{
  // Development: DOLPHIN_UT_OVERLAY_MENU=game|save|load|disc|main|toggle|latching|adjust|controller|
  // pointer|edit opens that menu page once the game draws, for checking its look from screen
  // captures over adb.
  {
    const QByteArray menu = qgetenv("DOLPHIN_UT_OVERLAY_MENU");
    static constexpr std::pair<const char*, Menu> pages[] = {
        {"game", Menu::Game},         {"save", Menu::SaveState},  {"load", Menu::LoadState},
        {"disc", Menu::ChangeDisc},   {"main", Menu::Main},         {"toggle", Menu::Toggle},   {"latching", Menu::Latching},
        {"adjust", Menu::Adjust},     {"controller", Menu::Controller},
        {"pointer", Menu::Pointer},   {"edit", Menu::Editing}};
    for (const auto& [name, page] : pages)
    {
      if (menu == name)
        OpenPage(page);
    }
  }
}

TouchOverlay::~TouchOverlay()
{
  *m_alive = false;
  m_draw_hook.reset();
  std::lock_guard lock(m_mutex);
  Teardown();
  // The textures belonged to the ImGui context of the game's on-screen UI, which is gone by now
  // (the overlay is destroyed after the game has stopped), along with their GPU textures.
  m_textures.clear();
  m_retired_textures.clear();
}

OverlayLayout::Type TouchOverlay::GetType() const
{
  std::lock_guard lock(m_mutex);
  return m_type;
}

int TouchOverlay::GetControllerIndex() const
{
  std::lock_guard lock(m_mutex);
  return m_controller_index;
}

void TouchOverlay::Build()
{
  m_type = OverlayLayout::ShowOverlay() ? OverlayLayout::ConfiguredType(m_wii) : Type::None;
  m_controller_index = OverlayLayout::ControllerIndex(m_wii);
  m_size_scale = (OverlayLayout::ControlScale() + 50) / 100.0f;
  m_relative_center = OverlayLayout::JoystickRelativeCenter();
  m_alpha = static_cast<u32>(std::clamp(OverlayLayout::ControlOpacity(), 0, 100) * 255 / 100);

  m_images.clear();
  m_images_changed = true;
  m_controls.clear();
  for (const OverlayLayout::ControlDef& def : OverlayLayout::GetControls(m_type))
  {
    if (!OverlayLayout::ControlShown(def))
      continue;
    Control control;
    control.def = def;
    control.latching = def.kind == Kind::Button && OverlayLayout::ControlLatching(def);

    // InputOverlay.resizeBitmap: the image's longer side is size × the screen's shorter side.
    const int max_side = static_cast<int>(def.size * m_size_scale * m_screen_short_side);
    if (def.kind == Kind::Joystick)
    {
      const int inner =
          static_cast<int>(max_side / OverlayLayout::JoystickInnerScale(def.button_type));
      control.images = {AddImage(def.images[0], max_side), AddImage(def.images[1], inner),
                        AddImage(def.images[2], inner)};
    }
    else
    {
      for (size_t i = 0; i < 3; ++i)
        control.images[i] = def.images[i] ? AddImage(def.images[i], max_side) : -1;
    }
    m_controls.push_back(std::move(control));
  }

  m_has_pointer = false;
  if (m_type == Type::GameCube)
  {
    ciface::Touch::RegisterGameCubeInputOverrider(m_controller_index);
  }
  else if (m_type != Type::None)
  {
    ciface::Touch::RegisterWiiInputOverrider(m_controller_index);

    // InputOverlay.initTouchPointer: the pointer for every Wii layout, unless turned off.
    m_pointer = {};
    m_pointer.mode = OverlayLayout::IRMode();
    m_has_pointer = m_pointer.mode != 0;
    m_pointer.recenter = OverlayLayout::IRAlwaysRecenter();
    int button = OverlayLayout::DoubleTapButton();
    if (m_type != Type::WiiRemoteClassic && button == BUTTON_TYPE_CLASSIC_A)
      button = BUTTON_TYPE_WIIMOTE_A;
    m_pointer.double_tap_control = button == BUTTON_TYPE_WIIMOTE_B ? ControlID::WIIMOTE_B_BUTTON :
                                   button == BUTTON_TYPE_WIIMOTE_2 ? ControlID::WIIMOTE_TWO_BUTTON :
                                   button == BUTTON_TYPE_CLASSIC_A ? ControlID::CLASSIC_A_BUTTON :
                                                                     ControlID::WIIMOTE_A_BUTTON;
  }

  Layout();

  static constexpr const char* type_names[] = {"GameCube",           "Wii Remote",
                                               "Wii Remote sideways", "Wii Remote + Nunchuk",
                                               "Classic Controller",  "none"};
  Emulator::LogFromAnyThread(
      QStringLiteral("overlay: %1, controller %2, %3 controls, %4 images, scale %5, opacity %6, "
                     "pointer %7")
          .arg(QString::fromLatin1(type_names[static_cast<int>(m_type)]))
          .arg(m_controller_index + 1)
          .arg(m_controls.size())
          .arg(m_images.size())
          .arg(m_size_scale)
          .arg(m_alpha)
          .arg(m_has_pointer ? m_pointer.mode : 0));
}

void TouchOverlay::Teardown()
{
  ReleaseAll();
  if (m_type == Type::GameCube)
    ciface::Touch::UnregisterGameCubeInputOverrider(m_controller_index);
  else if (m_type != Type::None)
    ciface::Touch::UnregisterWiiInputOverrider(m_controller_index);
  m_type = Type::None;
}

void TouchOverlay::ReleaseAll()
{
  for (Control& control : m_controls)
  {
    if (control.track < 0 && !control.pressed && !control.dpad)
      continue;
    // Latched buttons are let go too (the game should not keep a button held through a rebuild).
    const bool latching = control.latching;
    control.latching = false;
    Release(control);
    control.latching = latching;
  }
  m_menu_button.track = -1;
  m_menu_button.pressed = false;
  if (m_pointer.track >= 0)
    ReleasePointer();
}

void TouchOverlay::ToggleMenu()
{
  std::lock_guard lock(m_mutex);
  // A question stays until it is answered.
  if (m_menu == Menu::Question)
    return;
  // As the Android app's Back: closes whatever page is open, or opens the in-game menu.
  if (m_menu == Menu::None)
  {
    ReleaseAll();
    OpenPage(Menu::Game);
  }
  else
  {
    m_menu = Menu::None;
  }
}

void TouchOverlay::ShowQuestion(int id, std::string caption, std::string text, bool yes_no,
                                bool warning, std::function<void(int id, int button)> answer)
{
  std::lock_guard lock(m_mutex);
  // Nothing stays held while the game waits for the answer.
  ReleaseAll();
  const Menu before = m_menu == Menu::Question ? m_question.menu_before : m_menu;
  m_question = {id, std::move(caption), std::move(text), yes_no, warning, std::move(answer), before};
  m_menu = Menu::Question;
}

void TouchOverlay::CloseQuestion(int id)
{
  std::lock_guard lock(m_mutex);
  if (m_menu == Menu::Question && m_question.id == id)
    m_menu = m_question.menu_before;
}

bool TouchOverlay::WantsPausedRedraw() const
{
  std::lock_guard lock(m_mutex);
  return m_menu != Menu::None;
}

void TouchOverlay::OpenPage(Menu page)
{
  m_menu = page;
  if (page == Menu::SaveState || page == Menu::LoadState)
  {
    // SaveLoadStateFragment: six slots with the time each was saved ("Slot 1 / Empty").
    m_slot_labels.clear();
    const auto now = std::chrono::system_clock::now();
    for (int slot = 0; slot < 6; ++slot)
    {
      // State::GetUnixTimeOfSlot shows "State not found" for a missing file, once per empty slot
      // (S3.4-M3), so look for the file first, as State::GetInfoStringOfSlot does.
      const std::string file =
          fmt::format("{}{}.s{:02d}", File::GetUserPath(D_STATESAVES_IDX),
                      SConfig::GetInstance().GetGameID(), slot);
      const u64 saved_ms =
          File::Exists(file) ? State::GetUnixTimeOfSlot(static_cast<u32>(slot)) : 0;
      std::string when = "Empty";
      if (saved_ms != 0)
      {
        const auto saved = std::chrono::system_clock::time_point(std::chrono::milliseconds(saved_ms));
        const auto minutes =
            std::chrono::duration_cast<std::chrono::minutes>(now - saved).count();
        if (minutes < 1)
          when = "Just now";
        else if (minutes < 60)
          when = std::to_string(minutes) + " min. ago";
        else if (minutes < 60 * 24)
          when = std::to_string(minutes / 60) + " h ago";
        else
          when = std::to_string(minutes / (60 * 24)) + " days ago";
      }
      m_slot_labels.push_back("Slot " + std::to_string(slot + 1) + "\n" + when);
    }
  }
  else if (page == Menu::ChangeDisc)
  {
    // The Android app opens a file picker; here, the disc images in the game folders.
    m_discs.clear();
    const std::vector<std::string> folders = Config::GetIsoPaths();
    const std::vector<std::string_view> views(folders.begin(), folders.end());
    for (std::string& path :
         UICommon::FindAllGamePaths(views, Config::Get(Config::MAIN_RECURSIVE_ISO_PATHS)))
    {
      static constexpr std::string_view not_discs[] = {".wad", ".dol", ".elf", ".json"};
      std::string lower = path;
      Common::ToLower(&lower);
      if (std::ranges::none_of(not_discs, [&](std::string_view ext) { return lower.ends_with(ext); }))
        m_discs.push_back(std::move(path));
    }
    std::ranges::sort(m_discs);
  }
}

void TouchOverlay::RunOnUIThread(std::function<void()> action)
{
  if (!action)
    return;
  QMetaObject::invokeMethod(
      QCoreApplication::instance(),
      [alive = m_alive, action = std::move(action)] {
        if (*alive)
          action();
      },
      Qt::QueuedConnection);
}

void TouchOverlay::ChangeSettings(std::function<void()> change)
{
  // Settings are written on the UI thread, where the rest of the frontend writes them; then the
  // overlay is rebuilt from them, as the Android app's refreshInputOverlay.
  QMetaObject::invokeMethod(
      QCoreApplication::instance(),
      [this, alive = m_alive, change = std::move(change)] {
        if (!*alive)
          return;
        change();
        std::lock_guard lock(m_mutex);
        Teardown();
        Build();
      },
      Qt::QueuedConnection);
}

int TouchOverlay::AddImage(const char* name, int max_side)
{
  for (size_t i = 0; i < m_images.size(); ++i)
  {
    if (m_images[i].name == name && std::max(m_images[i].width, m_images[i].height) == max_side)
      return static_cast<int>(i);
  }

  QImage image(QStringLiteral(":/overlay/%1.png").arg(QString::fromLatin1(name)));
  if (image.isNull() || max_side <= 0)
  {
    Emulator::LogFromAnyThread(
        QStringLiteral("overlay: image %1 missing").arg(QString::fromLatin1(name)));
    return -1;
  }
  // Scaled once on the CPU, as the Android app does, so the GPU draws it at its size.
  image = image.scaled(max_side, max_side, Qt::KeepAspectRatio, Qt::SmoothTransformation)
              .convertToFormat(QImage::Format_RGBA8888);

  Image entry;
  entry.name = name;
  entry.width = image.width();
  entry.height = image.height();
  entry.rgba.resize(static_cast<size_t>(entry.width) * entry.height * 4);
  for (int y = 0; y < entry.height; ++y)
  {
    std::memcpy(entry.rgba.data() + static_cast<size_t>(y) * entry.width * 4,
                image.constScanLine(y), static_cast<size_t>(entry.width) * 4);
  }
  m_images.push_back(std::move(entry));
  return static_cast<int>(m_images.size() - 1);
}

void TouchOverlay::Layout()
{
  if (m_window_size.isEmpty())
    return;
  const double width = m_window_size.width();
  const double height = m_window_size.height();
  const bool portrait = Portrait();

  for (Control& control : m_controls)
  {
    // A position moved in edit mode, else the Android default (top-left corners as fractions of
    // the window), else the corner (the Android app's 0 default).
    auto position = OverlayLayout::SavedPosition(m_type, control.def.button_type, portrait);
    if (!position)
      position = OverlayLayout::DefaultPosition(m_type, control.def.button_type, portrait);
    const double x = position ? position->first * width : 0.0;
    const double y = position ? position->second * height : 0.0;
    const Image* image = control.images[0] >= 0 ? &m_images[control.images[0]] : nullptr;
    const double side = control.def.size * m_size_scale * m_screen_short_side;
    control.rect = QRectF(x, y, image ? image->width : side, image ? image->height : side);
    control.virtual_rect = control.rect;
  }

  // The in-game menu's button: small and round at the top centre, free in every Android layout.
  const double short_side = std::min(width, height);
  const double radius = 0.04 * short_side;
  m_menu_button.rect = QRectF(width / 2 - radius, 0.04 * short_side, radius * 2, radius * 2);

  // Edit mode's "Done", in the middle, as the Android app's done_control_config.
  const QSizeF done(0.24 * short_side, 0.1 * short_side);
  m_done_rect = QRectF(QPointF(width / 2 - done.width() / 2, height / 2 - done.height() / 2), done);
}

void TouchOverlay::SetWindowSize(QSizeF window_size)
{
  std::lock_guard lock(m_mutex);
  if (window_size == m_window_size || window_size.isEmpty())
    return;
  ReleaseAll();
  m_window_size = window_size;
  Layout();
}

void TouchOverlay::HandleTouch(const QList<QEventPoint>& points, QSizeF window_size,
                               qreal device_pixel_ratio, bool cancel)
{
  std::vector<ImGuiInput> imgui_input;
  {
    std::lock_guard lock(m_mutex);
    if (window_size != m_window_size && !window_size.isEmpty())
    {
      ReleaseAll();
      m_window_size = window_size;
      Layout();
    }

    if (cancel)
    {
      ReleaseAll();
      m_drag_control = m_drag_touch = m_done_touch = -1;
      if (m_menu_touch >= 0)
        imgui_input.push_back({QPointF(-FLT_MAX, -FLT_MAX), 0});
      m_menu_touch = -1;
    }

    for (const QEventPoint& point : cancel ? QList<QEventPoint>() : points)
    {
      // Touch positions are window-relative logical pixels; the layout is in physical ones.
      const QPointF position = point.position() * device_pixel_ratio;
      const int id = point.id();

      if (m_menu == Menu::Editing)
      {
        HandleEditTouch(point, position);
        continue;
      }

      if (m_menu != Menu::None)
      {
        // Ubuntu Touch has no system Back: a tap beside the in-game menu's panel closes it.
        const bool game_menu = m_menu == Menu::Game || m_menu == Menu::SaveState ||
                               m_menu == Menu::LoadState || m_menu == Menu::ChangeDisc;
        if (game_menu && point.state() == QEventPoint::State::Pressed && m_menu_touch < 0 &&
            m_game_panel_width > 0 && position.x() > m_game_panel_width)
        {
          m_menu = Menu::None;
          continue;
        }
        // The menu is ImGui: its first finger becomes ImGui's mouse.
        if (point.state() == QEventPoint::State::Pressed && m_menu_touch < 0)
        {
          m_menu_touch = id;
          imgui_input.push_back({position, -1});
          imgui_input.push_back({position, 1});
        }
        else if (id == m_menu_touch && point.state() == QEventPoint::State::Updated)
        {
          imgui_input.push_back({position, -1});
        }
        else if (id == m_menu_touch && point.state() == QEventPoint::State::Released)
        {
          imgui_input.push_back({position, 0});
          // No hovering once the finger is up.
          imgui_input.push_back({QPointF(-FLT_MAX, -FLT_MAX), -1});
          m_menu_touch = -1;
        }
        continue;
      }

      switch (point.state())
      {
      case QEventPoint::State::Pressed:
      {
        bool pressed = false;
        if (m_menu_button.rect.contains(position) && m_menu_button.track < 0)
        {
          m_menu_button.track = id;
          m_menu_button.pressed = true;
          pressed = true;
        }
        const bool action_hit = pressed;
        // As the Android app: every control under the finger is pressed.
        for (Control& control : m_controls)
        {
          if (!action_hit && control.track < 0 && control.rect.contains(position))
          {
            Press(control, position, id);
            pressed = true;
          }
        }
        // No control under the finger: it moves the Wii Remote's pointer.
        if (!pressed && m_has_pointer && m_pointer.track < 0)
          PressPointer(position, id);
        break;
      }
      case QEventPoint::State::Updated:
        for (Control& control : m_controls)
        {
          if (control.track == id)
            Move(control, position);
        }
        if (m_pointer.track == id)
          MovePointer(position);
        break;
      case QEventPoint::State::Released:
        if (m_menu_button.track == id)
        {
          m_menu_button.track = -1;
          m_menu_button.pressed = false;
          if (m_menu_button.rect.contains(position))
          {
            ReleaseAll();
            OpenPage(Menu::Game);
          }
        }
        for (Control& control : m_controls)
        {
          if (control.track == id)
            Release(control);
        }
        if (m_pointer.track == id)
          ReleasePointer();
        break;
      default:
        break;
      }
    }
  }

  // ImGui's lock is taken outside ours (the video thread holds it while drawing, then takes ours).
  for (const ImGuiInput& input : imgui_input)
  {
    if (!g_presenter)
      break;
    g_presenter->SetMousePos(static_cast<float>(input.position.x()),
                             static_cast<float>(input.position.y()));
    if (input.press >= 0)
      g_presenter->SetMousePress(input.press ? 1u : 0u);
  }
}

void TouchOverlay::HandleEditTouch(const QEventPoint& point, QPointF position)
{
  // InputOverlay.onTouchWhileEditing: one control at a time follows the finger, and its position
  // is saved when the finger lifts.
  const int id = point.id();
  switch (point.state())
  {
  case QEventPoint::State::Pressed:
    if (m_done_rect.contains(position))
    {
      m_done_touch = id;
    }
    else if (m_drag_touch < 0)
    {
      // The control drawn last (on top) wins.
      for (int i = static_cast<int>(m_controls.size()) - 1; i >= 0; --i)
      {
        if (m_controls[i].rect.contains(position))
        {
          m_drag_control = i;
          m_drag_touch = id;
          m_drag_last = position;
          break;
        }
      }
    }
    break;
  case QEventPoint::State::Updated:
    if (id == m_drag_touch && m_drag_control >= 0)
    {
      Control& control = m_controls[m_drag_control];
      control.rect.translate(position - m_drag_last);
      control.virtual_rect = control.rect;
      m_drag_last = position;
    }
    break;
  case QEventPoint::State::Released:
    if (id == m_done_touch)
    {
      m_done_touch = -1;
      if (m_done_rect.contains(position))
        m_menu = Menu::None;
    }
    if (id == m_drag_touch && m_drag_control >= 0)
    {
      const Control& control = m_controls[m_drag_control];
      OverlayLayout::SavePosition(m_type, control.def.button_type, Portrait(),
                                  static_cast<float>(control.rect.x() / m_window_size.width()),
                                  static_cast<float>(control.rect.y() / m_window_size.height()));
      m_drag_control = m_drag_touch = -1;
    }
    break;
  default:
    break;
  }
}

void TouchOverlay::Press(Control& control, QPointF position, int id)
{
  control.track = id;
  switch (control.def.kind)
  {
  case Kind::Button:
  {
    control.pressed = control.latching ? !control.pressed : true;
    const ControlID id_control = control.def.controls[0];
    ciface::Touch::SetControlState(m_controller_index, id_control, control.pressed ? 1.0 : 0.0);
    // The Android app also presses the analog trigger fully with the digital one. Unlike the
    // Android app, the analog trigger follows a latched button too (there it is only held while
    // the finger is down, so a latched R gave no throttle in Need for Speed, S3.3-M5).
    if (const auto analog = OverlayLayout::AnalogForTrigger(id_control))
      ciface::Touch::SetControlState(m_controller_index, *analog, control.pressed ? 1.0 : 0.0);
    break;
  }
  case Kind::Dpad:
    UpdateDpad(control, position);
    break;
  case Kind::Joystick:
    control.pressed = true;
    if (m_relative_center)
      control.virtual_rect.moveCenter(position);
    UpdateJoystick(control, position);
    break;
  }
}

void TouchOverlay::Move(Control& control, QPointF position)
{
  if (control.def.kind == Kind::Dpad)
    UpdateDpad(control, position);
  else if (control.def.kind == Kind::Joystick)
    UpdateJoystick(control, position);
}

void TouchOverlay::Release(Control& control)
{
  control.track = -1;
  switch (control.def.kind)
  {
  case Kind::Button:
  {
    if (!control.latching)
      control.pressed = false;
    const ControlID id_control = control.def.controls[0];
    ciface::Touch::SetControlState(m_controller_index, id_control, control.pressed ? 1.0 : 0.0);
    if (const auto analog = OverlayLayout::AnalogForTrigger(id_control))
      ciface::Touch::SetControlState(m_controller_index, *analog, control.pressed ? 1.0 : 0.0);
    break;
  }
  case Kind::Dpad:
    control.dpad = 0;
    for (size_t i = 0; i < 4; ++i)
      ciface::Touch::SetControlState(m_controller_index, control.def.controls[i], 0.0);
    break;
  case Kind::Joystick:
    control.pressed = false;
    control.stick_x = 0.0f;
    control.stick_y = 0.0f;
    control.virtual_rect = control.rect;
    ciface::Touch::SetControlState(m_controller_index, control.def.controls[0], 0.0);
    ciface::Touch::SetControlState(m_controller_index, control.def.controls[1], 0.0);
    break;
  }
}

void TouchOverlay::UpdateDpad(Control& control, QPointF position)
{
  // InputOverlay.onTouch: the outer thirds of the D-pad press its directions, diagonals included.
  const QRectF& rect = control.rect;
  u8 state = 0;
  if (position.y() < rect.top() + rect.height() / 3)
    state |= DPAD_UP;
  if (position.y() > rect.bottom() - rect.height() / 3)
    state |= DPAD_DOWN;
  if (position.x() < rect.left() + rect.width() / 3)
    state |= DPAD_LEFT;
  if (position.x() > rect.right() - rect.width() / 3)
    state |= DPAD_RIGHT;
  control.dpad = state;
  static constexpr u8 bits[] = {DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT};
  for (size_t i = 0; i < 4; ++i)
  {
    ciface::Touch::SetControlState(m_controller_index, control.def.controls[i],
                                   (state & bits[i]) ? 1.0 : 0.0);
  }
}

void TouchOverlay::UpdateJoystick(Control& control, QPointF position)
{
  // InputOverlayDrawableJoystick: -1..1 across the range, kept inside the stick's gate.
  const QRectF& range = control.virtual_rect;
  double x = (position.x() - range.center().x()) / (range.width() / 2);
  double y = (position.y() - range.center().y()) / (range.height() / 2);
  const double angle = std::atan2(y, x) + 2 * std::numbers::pi;
  const double radius = std::hypot(x, y);
  const double max_radius =
      ciface::Touch::GetGateRadiusAtAngle(m_controller_index, control.def.controls[0], angle);
  if (radius > max_radius)
  {
    x = max_radius * std::cos(angle);
    y = max_radius * std::sin(angle);
  }
  control.stick_x = static_cast<float>(x);
  control.stick_y = static_cast<float>(y);
  ciface::Touch::SetControlState(m_controller_index, control.def.controls[0], x);
  ciface::Touch::SetControlState(m_controller_index, control.def.controls[1], -y);
}

void TouchOverlay::PressPointer(QPointF position, int id)
{
  m_pointer.track = id;
  m_pointer.touch_start = position;
  // InputOverlayPointer.touchPress: a second tap within 300 ms presses the double-tap button.
  if (m_pointer.last_tap.isValid() && m_pointer.last_tap.elapsed() < DOUBLE_TAP_MS)
  {
    m_pointer.last_tap.invalidate();
    const int index = m_controller_index;
    const ControlID control = m_pointer.double_tap_control;
    ciface::Touch::SetControlState(index, control, 1.0);
    QTimer::singleShot(DOUBLE_TAP_PRESS_MS,
                       [index, control] { ciface::Touch::SetControlState(index, control, 0.0); });
  }
  else
  {
    m_pointer.last_tap.start();
  }
  MovePointer(position);
}

void TouchOverlay::MovePointer(QPointF position)
{
  if (m_game_rect.isEmpty())
    return;
  const double half_width_inverse = 2.0 / m_game_rect.width();
  const double half_height_inverse = 2.0 / m_game_rect.height();
  if (m_pointer.mode == 1)
  {
    m_pointer.x = static_cast<float>((position.x() - m_game_rect.center().x()) * half_width_inverse);
    m_pointer.y = static_cast<float>((position.y() - m_game_rect.center().y()) * half_height_inverse);
  }
  else if (m_pointer.mode == 2)
  {
    m_pointer.x = static_cast<float>(m_pointer.old_x + (position.x() - m_pointer.touch_start.x()) *
                                                           half_width_inverse);
    m_pointer.y = static_cast<float>(m_pointer.old_y + (position.y() - m_pointer.touch_start.y()) *
                                                           half_height_inverse);
  }
  SendPointer();
}

void TouchOverlay::ReleasePointer()
{
  m_pointer.track = -1;
  if (m_pointer.mode == 2)
  {
    m_pointer.old_x = m_pointer.x;
    m_pointer.old_y = m_pointer.y;
  }
  if (m_pointer.recenter)
  {
    m_pointer.x = m_pointer.y = m_pointer.old_x = m_pointer.old_y = 0.0f;
    SendPointer();
  }
}

void TouchOverlay::SendPointer()
{
  ciface::Touch::SetControlState(m_controller_index, ControlID::WIIMOTE_IR_X, m_pointer.x);
  ciface::Touch::SetControlState(m_controller_index, ControlID::WIIMOTE_IR_Y, -m_pointer.y);
}

void TouchOverlay::RetireTextures()
{
  // Replaced textures are freed by Dolphin's ImGui backend (OnScreenUI::UpdateImguiTexture) once
  // they are marked for destruction and unused for a frame; then they leave ImGui's list.
  std::erase_if(m_retired_textures, [](const std::unique_ptr<ImTextureData>& texture) {
    if (texture->Status == ImTextureStatus_Destroyed ||
        (texture->Status == ImTextureStatus_WantCreate && texture->TexID == ImTextureID_Invalid))
    {
      ImGui::UnregisterUserTexture(texture.get());
      return true;
    }
    if (texture->Status != ImTextureStatus_WantDestroy)
    {
      texture->Status = ImTextureStatus_WantDestroy;
      texture->UnusedFrames = 1;
    }
    return false;
  });
}

void TouchOverlay::Draw()
{
  // The hook can run more than once per ImGui frame (once per present); draw into each frame once,
  // or the vertices pile up (stage 3.0's Vulkan assert).
  ImGuiContext* context = ImGui::GetCurrentContext();
  if (!context || !context->WithinFrameScope)
    return;
  const int frame = ImGui::GetFrameCount();
  if (frame == m_last_drawn_frame)
    return;
  m_last_drawn_frame = frame;

  const auto start = std::chrono::steady_clock::now();
  std::lock_guard lock(m_mutex);
  if (!m_test_page_checked)
  {
    m_test_page_checked = true;
    OpenTestPage();
  }

  // The images become ImGui user textures: Dolphin's ImGui backend uploads them, and frees them with
  // the on-screen UI when the game stops. After a rebuild, the old ones are retired.
  RetireTextures();
  if (m_images_changed)
  {
    for (auto& texture : m_textures)
      m_retired_textures.push_back(std::move(texture));
    m_textures.clear();
    RetireTextures();
    for (const Image& image : m_images)
    {
      auto texture = std::make_unique<ImTextureData>();
      texture->Create(ImTextureFormat_RGBA32, image.width, image.height);
      std::memcpy(texture->Pixels, image.rgba.data(), image.rgba.size());
      ImGui::RegisterUserTexture(texture.get());
      m_textures.push_back(std::move(texture));
    }
    m_images_changed = false;
  }

  if (g_presenter)
  {
    const auto& target = g_presenter->GetTargetRectangle();
    m_game_rect = QRectF(target.left, target.top, target.GetWidth(), target.GetHeight());
  }

  // The background list: over the game picture, under ImGui windows (the menu, Dolphin's OSD).
  ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
  const u32 alpha = m_opacity_preview >= 0 ? static_cast<u32>(m_opacity_preview * 255 / 100) : m_alpha;
  const ImU32 tint = IM_COL32(255, 255, 255, alpha);
  const auto image = [&](int index, const QRectF& rect, int quarter_turns = 0) {
    if (index < 0 || static_cast<size_t>(index) >= m_textures.size())
      return;
    const ImVec2 corners[4] = {ToImVec2(rect.topLeft()), ToImVec2(rect.topRight()),
                               ToImVec2(rect.bottomRight()), ToImVec2(rect.bottomLeft())};
    static constexpr ImVec2 uvs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    // Clockwise quarter turns: corner i shows the image's corner i - turns.
    const auto uv = [&](int corner) { return uvs[(corner - quarter_turns + 4) % 4]; };
    draw_list->AddImageQuad(m_textures[index]->GetTexRef(), corners[0], corners[1], corners[2],
                            corners[3], uv(0), uv(1), uv(2), uv(3), tint);
  };

  const bool editing = m_menu == Menu::Editing;
  for (size_t i = 0; i < m_controls.size(); ++i)
  {
    const Control& control = m_controls[i];
    switch (control.def.kind)
    {
    case Kind::Button:
      image(control.pressed && control.images[1] >= 0 ? control.images[1] : control.images[0],
            control.rect);
      break;
    case Kind::Dpad:
    {
      // InputOverlayDrawableDpad.draw: the one- and two-direction images, turned to the direction.
      const u8 s = control.dpad;
      const bool up = s & DPAD_UP, down = s & DPAD_DOWN, left = s & DPAD_LEFT,
                 right = s & DPAD_RIGHT;
      if (up && left)
        image(control.images[2], control.rect, 0);
      else if (up && right)
        image(control.images[2], control.rect, 1);
      else if (down && right)
        image(control.images[2], control.rect, 2);
      else if (down && left)
        image(control.images[2], control.rect, 3);
      else if (up)
        image(control.images[1], control.rect, 0);
      else if (right)
        image(control.images[1], control.rect, 1);
      else if (down)
        image(control.images[1], control.rect, 2);
      else if (left)
        image(control.images[1], control.rect, 3);
      else
        image(control.images[0], control.rect, 0);
      break;
    }
    case Kind::Joystick:
    {
      // The range follows the finger with "relative center"; the inner part follows the stick.
      const QRectF& range = control.virtual_rect;
      image(control.images[0], range);
      const int inner_index = control.pressed ? control.images[2] : control.images[1];
      if (inner_index >= 0)
      {
        const Image& inner = m_images[inner_index];
        const QPointF center = range.center() + QPointF(control.stick_x * range.width() / 2,
                                                        control.stick_y * range.height() / 2);
        image(inner_index, QRectF(center.x() - inner.width / 2.0, center.y() - inner.height / 2.0,
                                  inner.width, inner.height));
      }
      break;
    }
    }
    if (editing)
    {
      // Edit mode: each control's bounds, the dragged one highlighted.
      const bool dragged = static_cast<int>(i) == m_drag_control;
      draw_list->AddRect(ToImVec2(control.rect.topLeft()), ToImVec2(control.rect.bottomRight()),
                         dragged ? IM_COL32(255, 200, 60, 230) : IM_COL32(255, 255, 255, 110), 6.0f,
                         0, dragged ? 4.0f : 2.0f);
    }
  }

  const auto round_button = [&](const QRectF& rect, const char* label, bool pressed) {
    const ImVec2 center = ToImVec2(rect.center());
    const float radius = static_cast<float>(std::min(rect.width(), rect.height()) / 2);
    if (rect.width() > rect.height() * 1.2)
    {
      const float rounding = static_cast<float>(rect.height() / 2);
      draw_list->AddRectFilled(ToImVec2(rect.topLeft()), ToImVec2(rect.bottomRight()),
                               pressed ? IM_COL32(255, 255, 255, 170) : IM_COL32(0, 0, 0, 170),
                               rounding);
      draw_list->AddRect(ToImVec2(rect.topLeft()), ToImVec2(rect.bottomRight()),
                         IM_COL32(255, 255, 255, 200), rounding, 0, 3.0f);
    }
    else
    {
      draw_list->AddCircleFilled(center, radius,
                                 pressed ? IM_COL32(255, 255, 255, 140) : IM_COL32(0, 0, 0, 90));
      draw_list->AddCircle(center, radius, IM_COL32(255, 255, 255, 150), 0, 2.0f);
    }
    const float font_size = std::clamp(radius * (std::strlen(label) > 4 ? 0.45f : 0.55f), 12.0f, 48.0f);
    const ImVec2 text_size = ImGui::GetFont()->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label);
    draw_list->AddText(ImGui::GetFont(), font_size,
                       ImVec2(center.x - text_size.x / 2, center.y - text_size.y / 2),
                       IM_COL32(255, 255, 255, 230), label);
  };

  if (editing)
  {
    round_button(m_done_rect, "Done", m_done_touch >= 0);
  }
  else if (m_menu == Menu::None)
  {
    round_button(m_menu_button.rect, "Menu", m_menu_button.pressed);
  }

  // Dolphin's messages move right of the in-game menu panel, as with the Android app's menu.
  const bool game_menu = m_menu == Menu::Game || m_menu == Menu::SaveState ||
                         m_menu == Menu::LoadState || m_menu == Menu::ChangeDisc;
  if (game_menu)
    DrawGameMenu();
  else
    OSD::SetObscuredPixelsLeft(0);
  if (m_menu == Menu::Question)
    DrawQuestion();
  else if (m_menu != Menu::None && m_menu != Menu::Editing && !game_menu)
    DrawMenu();

  m_draw_time += std::chrono::steady_clock::now() - start;
  if (++m_draw_count == 600)
  {
    Emulator::LogFromAnyThread(
        QStringLiteral("overlay: %1 us per frame (average of 600)")
            .arg(std::chrono::duration_cast<std::chrono::microseconds>(m_draw_time).count() / 600.0,
                 0, 'f', 1));
    m_draw_time = {};
    m_draw_count = 0;
  }
}

void TouchOverlay::DrawMenu()
{
  // The Android app's "Overlay Controls" menu and dialogs (EmulationActivity), with its labels, as
  // one ImGui window in the middle of the game picture, sized for fingers.
  const float short_side = static_cast<float>(std::min(m_window_size.width(), m_window_size.height()));
  const float ui_scale = std::max(ImGui::GetStyle().FontScaleMain, 1.0f);
  // About 4.5 % of the screen's shorter side per line of text.
  const float font_size = std::max(short_side * 0.045f / ui_scale, 12.0f);
  ImGui::PushFont(nullptr, font_size);
  const float line = ImGui::GetFontSize();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(line * 0.8f, line * 0.7f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(line * 0.5f, line * 0.35f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(line * 0.5f, line * 0.35f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(line * 0.4f, line * 0.3f));
  ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, line * 1.1f);
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, line * 0.8f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, line * 0.4f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, line * 0.25f);
  ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, line * 0.25f);

  const bool landscape = !Portrait();
  const float width = std::min(static_cast<float>(m_window_size.width()) * 0.92f,
                               line * (landscape ? 30.0f : 20.0f));
  const float max_height = static_cast<float>(m_window_size.height()) * 0.9f;
  ImGui::SetNextWindowPos(ImVec2(static_cast<float>(m_window_size.width()) / 2,
                                 static_cast<float>(m_window_size.height()) / 2),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, max_height));
  ImGui::SetNextWindowBgAlpha(0.94f);

  const char* titles[] = {"",          "",
                          "",          "",
                          "",          "Overlay Controls",
                          "Toggle Controls", "Latching Controls",
                          "Adjust Controls", "Choose Controller",
                          "Touch IR Pointer", ""};
  const std::string title =
      std::string(titles[static_cast<int>(m_menu)]) + "###dolphin-ut-overlay-menu";
  constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_AlwaysAutoResize |
                                     // Pages are laid out to fit (columns in landscape); ImGui's
                                     // scrollbar showed even then and cannot be dragged by touch.
                                     ImGuiWindowFlags_NoScrollbar;
  if (ImGui::Begin(title.c_str(), nullptr, flags))
  {
    const ImVec2 full(-FLT_MIN, 0);
    const auto back = [&] {
      ImGui::Spacing();
      if (ImGui::Button("Back", full))
        m_menu = Menu::Main;
    };

    switch (m_menu)
    {
    case Menu::Main:
    {
      // In landscape the entries go in two columns, to fit the height (ImGui cannot scroll by
      // dragging, so everything must be visible).
      if (landscape)
        ImGui::Columns(2, nullptr, false);
      const auto next = [&] {
        if (landscape)
          ImGui::NextColumn();
      };
      bool show = OverlayLayout::ShowOverlay();
      if (ImGui::Checkbox("Show Overlay", &show))
        ChangeSettings([show] { OverlayLayout::SetShowOverlay(show); });
      next();
      bool relative = OverlayLayout::JoystickRelativeCenter();
      if (ImGui::Checkbox("Relative Stick Center", &relative))
        ChangeSettings([relative] { OverlayLayout::SetJoystickRelativeCenter(relative); });
      next();
      if (ImGui::Button("Edit Layout", full))
        m_menu = Menu::Editing;
      next();
      if (ImGui::Button("Toggle Controls", full))
        m_menu = Menu::Toggle;
      next();
      if (ImGui::Button("Latching Controls", full))
        m_menu = Menu::Latching;
      next();
      if (ImGui::Button("Adjust Controls", full))
        m_menu = Menu::Adjust;
      next();
      if (ImGui::Button("Choose Controller", full))
        m_menu = Menu::Controller;
      next();
      if (m_wii)
      {
        if (ImGui::Button("Touch IR Pointer", full))
          m_menu = Menu::Pointer;
        next();
      }
      if (ImGui::Button("Reset Overlay", full))
      {
        const Type type = m_type;
        const bool portrait = Portrait();
        ChangeSettings([type, portrait] { OverlayLayout::ResetPositions(type, portrait); });
      }
      next();
      if (ImGui::Button("Back", full))
        m_menu = Menu::Game;
      if (landscape)
        ImGui::Columns(1);
      break;
    }

    case Menu::Toggle:
    case Menu::Latching:
    {
      const bool toggle = m_menu == Menu::Toggle;
      const auto controls = OverlayLayout::GetControls(m_type);
      if (controls.empty())
        ImGui::TextWrapped("No overlay for the chosen controller.");
      // Two columns: the longest list (Classic Controller, 14) fits in landscape.
      ImGui::Columns(2, nullptr, false);
      for (const OverlayLayout::ControlDef& def : controls)
      {
        if (!toggle && def.kind != Kind::Button)
          continue;
        bool value = toggle ? OverlayLayout::ControlShown(def) : OverlayLayout::ControlLatching(def);
        const std::string label =
            std::string(OverlayLayout::Label(def)) + "##" + def.group + std::to_string(def.index);
        if (ImGui::Checkbox(label.c_str(), &value))
        {
          ChangeSettings([def, value, toggle] {
            if (toggle)
              OverlayLayout::SetControlShown(def, value);
            else
              OverlayLayout::SetControlLatching(def, value);
          });
        }
        ImGui::NextColumn();
      }
      ImGui::Columns(1);
      back();
      break;
    }

    case Menu::Adjust:
    {
      // EmulationActivity.adjustScale: scale 50–200 % (setting 0–150), opacity 0–100 %.
      int scale = m_scale_preview >= 0 ? m_scale_preview : OverlayLayout::ControlScale();
      ImGui::Text("Scale: %d%%", scale + 50);
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::SliderInt("##scale", &scale, 0, 150, "");
      if (ImGui::IsItemActive())
        m_scale_preview = scale;
      if (ImGui::IsItemDeactivated())
      {
        m_scale_preview = -1;
        ChangeSettings([scale] { OverlayLayout::SetControlScale(scale); });
      }

      int opacity = m_opacity_preview >= 0 ? m_opacity_preview : OverlayLayout::ControlOpacity();
      ImGui::Text("Opacity: %d%%", opacity);
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::SliderInt("##opacity", &opacity, 0, 100, "");
      if (ImGui::IsItemActive())
        m_opacity_preview = opacity;
      if (ImGui::IsItemDeactivated())
      {
        m_opacity_preview = -1;
        m_alpha = static_cast<u32>(opacity * 255 / 100);
        ChangeSettings([opacity] { OverlayLayout::SetControlOpacity(opacity); });
      }

      ImGui::Spacing();
      if (ImGui::Button("Default Values", full))
        ChangeSettings([] { OverlayLayout::ResetScaleAndOpacity(); });
      if (ImGui::Button("OK", full))
        m_menu = Menu::Main;
      break;
    }

    case Menu::Controller:
    {
      const int current = m_wii ? OverlayLayout::OverlayWiiController() :
                                  OverlayLayout::OverlayGCController();
      const auto choices = OverlayLayout::ControllerChoices(m_wii);
      const bool columns = landscape && choices.size() > 5;
      if (columns)
        ImGui::Columns(2, nullptr, false);
      for (const auto& [value, name] : choices)
      {
        if (ImGui::RadioButton(name.c_str(), value == current) && value != current)
        {
          const bool wii = m_wii;
          ChangeSettings([wii, value] { OverlayLayout::SetOverlayController(wii, value); });
        }
        if (columns)
          ImGui::NextColumn();
      }
      if (columns)
        ImGui::Columns(1);
      back();
      break;
    }

    case Menu::Pointer:
    {
      bool recenter = OverlayLayout::IRAlwaysRecenter();
      if (ImGui::Checkbox("Always recenter", &recenter))
        ChangeSettings([recenter] { OverlayLayout::SetIRAlwaysRecenter(recenter); });

      if (landscape)
        ImGui::Columns(2, nullptr, false);
      ImGui::SeparatorText("IR Mode");
      const int mode = OverlayLayout::IRMode();
      static constexpr const char* modes[] = {"Disabled", "Follow", "Drag"};
      for (int i = 0; i < 3; ++i)
      {
        if (ImGui::RadioButton(modes[i], mode == i) && mode != i)
          ChangeSettings([i] { OverlayLayout::SetIRMode(i); });
      }

      if (landscape)
        ImGui::NextColumn();
      ImGui::SeparatorText("Double tap button");
      const int button = OverlayLayout::DoubleTapButton();
      std::vector<std::pair<int, const char*>> buttons = {
          {BUTTON_TYPE_WIIMOTE_A, "A"}, {BUTTON_TYPE_WIIMOTE_B, "B"}, {BUTTON_TYPE_WIIMOTE_2, "2"}};
      if (m_type == Type::WiiRemoteClassic)
        buttons.emplace_back(BUTTON_TYPE_CLASSIC_A, "Classic a");
      for (const auto& [value, name] : buttons)
      {
        if (ImGui::RadioButton(name, button == value) && button != value)
          ChangeSettings([value] { OverlayLayout::SetDoubleTapButton(value); });
      }
      if (landscape)
        ImGui::Columns(1);
      back();
      break;
    }

    default:
      break;
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(9);
  ImGui::PopFont();
}

void TouchOverlay::DrawQuestion()
{
  // DolphinQt's and the Android app's alert, in the middle of the picture, sized as the overlay's
  // dialogs: the caption, the text, then Yes/No or OK, and "Ignore for this session" for warnings.
  const float short_side = static_cast<float>(std::min(m_window_size.width(), m_window_size.height()));
  const float ui_scale = std::max(ImGui::GetStyle().FontScaleMain, 1.0f);
  ImGui::PushFont(nullptr, std::max(short_side * 0.045f / ui_scale, 12.0f));
  const float line = ImGui::GetFontSize();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(line * 0.9f, line * 0.8f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(line * 0.5f, line * 0.45f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(line * 0.5f, line * 0.45f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, line * 0.4f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, line * 0.25f);

  const bool landscape = !Portrait();
  const float width = std::min(static_cast<float>(m_window_size.width()) * 0.92f,
                               line * (landscape ? 28.0f : 20.0f));
  ImGui::SetNextWindowPos(ImVec2(static_cast<float>(m_window_size.width()) / 2,
                                 static_cast<float>(m_window_size.height()) / 2),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0),
                                      ImVec2(width, static_cast<float>(m_window_size.height()) * 0.9f));
  ImGui::SetNextWindowBgAlpha(0.96f);
  ImGui::SetNextWindowFocus();
  const std::string title = m_question.caption + "###dolphin-ut-question";
  constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_AlwaysAutoResize;
  if (ImGui::Begin(title.c_str(), nullptr, flags))
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(m_question.text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    int button = -1;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float half = (ImGui::GetContentRegionAvail().x - spacing) / 2;
    if (m_question.yes_no)
    {
      if (ImGui::Button("Yes", ImVec2(half, 0)))
        button = 1;
      ImGui::SameLine();
      if (ImGui::Button("No", ImVec2(half, 0)))
        button = 0;
    }
    else if (ImGui::Button("OK", ImVec2(-FLT_MIN, 0)))
    {
      button = 0;
    }
    if (m_question.warning && ImGui::Button("Ignore for this session", ImVec2(-FLT_MIN, 0)))
      button = 2;

    if (button >= 0)
    {
      // Gone at once; Emulator closes it too once the answer arrives.
      m_menu = m_question.menu_before;
      RunOnUIThread([answer = m_question.answer, id = m_question.id, button] {
        if (answer)
          answer(id, button);
      });
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(5);
  ImGui::PopFont();
}

void TouchOverlay::DrawGameMenu()
{
  // The Android app's in-game menu (MenuFragment): a panel along the left edge, the game's title on
  // top, then its entries with the Android labels. The game keeps running behind it unless paused,
  // as in the Android app.
  const float width_px = static_cast<float>(m_window_size.width());
  const float height_px = static_cast<float>(m_window_size.height());
  const float short_side = std::min(width_px, height_px);
  const bool landscape = width_px > height_px;
  const float ui_scale = std::max(ImGui::GetStyle().FontScaleMain, 1.0f);
  // A little smaller than the overlay menu: eleven entries must fit a landscape height.
  ImGui::PushFont(nullptr, std::max(short_side * 0.038f / ui_scale, 12.0f));
  const float line = ImGui::GetFontSize();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(line * 0.8f, line * 0.6f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(line * 0.6f, line * 0.32f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(line * 0.4f, line * 0.3f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, line * 0.25f);
  ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

  const float panel_width = landscape ? std::max(width_px * 0.3f, line * 14.0f) :
                                        std::min(width_px * 0.75f, line * 18.0f);
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(panel_width, height_px), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.94f);
  OSD::SetObscuredPixelsLeft(static_cast<int>(panel_width));
  m_game_panel_width = panel_width;

  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoScrollbar;
  if (ImGui::Begin("###dolphin-ut-game-menu", nullptr, flags))
  {
    const ImVec2 full(-FLT_MIN, 0);
    // The title, and "Close" beside it (there is no system Back to close the menu with).
    const char* close_label = "Close";
    const float close_width =
        ImGui::CalcTextSize(close_label).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - close_width -
                           ImGui::GetStyle().ItemSpacing.x);
    ImGui::TextUnformatted(m_actions.game_title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - close_width);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
    if (ImGui::Button(close_label))
      m_menu = Menu::None;
    ImGui::PopStyleVar();
    ImGui::Separator();

    const auto action = [&](const std::function<void()>& function, bool close) {
      RunOnUIThread(function);
      if (close)
        m_menu = Menu::None;
    };
    const bool savestates = Config::Get(Config::MAIN_ENABLE_SAVESTATES);

    switch (m_menu)
    {
    case Menu::Game:
    {
      const bool paused = Core::GetState(Core::System::GetInstance()) == Core::State::Paused;
      if (ImGui::Button(paused ? "Unpause Emulation" : "Pause Emulation", full))
        action(m_actions.toggle_pause, false);
      if (ImGui::Button("Take Screenshot", full))
        action(m_actions.screenshot, true);
      if (savestates)
      {
        if (ImGui::Button("Quick Save", full) && m_actions.save_state)
          action([save = m_actions.save_state] { save(9); }, true);
        if (ImGui::Button("Quick Load", full) && m_actions.load_state)
          action([load = m_actions.load_state] { load(9); }, true);
        if (ImGui::Button("Save State", full))
          OpenPage(Menu::SaveState);
        if (ImGui::Button("Load State", full))
          OpenPage(Menu::LoadState);
      }
      if (m_actions.open_settings && ImGui::Button("Settings", full))
        action(m_actions.open_settings, true);
      if (ImGui::Button("Overlay Controls", full))
        m_menu = Menu::Main;
      if (m_wii && ImGui::Button("Refresh Wii Remotes", full))
        action(m_actions.refresh_wiimotes, true);
      if (ImGui::Button("Change Disc", full))
        OpenPage(Menu::ChangeDisc);
      if (ImGui::Button("Reset Emulation", full))
        action(m_actions.reset, true);
      ImGui::Separator();
      if (ImGui::Button("Exit Emulation", full))
        action(m_actions.exit, true);
      break;
    }

    case Menu::SaveState:
    case Menu::LoadState:
    {
      // SaveLoadStateFragment: a grid of six slots.
      const bool save = m_menu == Menu::SaveState;
      ImGui::TextUnformatted(save ? "Save State" : "Load State");
      const float cell = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
      for (int slot = 0; slot < static_cast<int>(m_slot_labels.size()); ++slot)
      {
        if (slot % 2 == 1)
          ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
        const bool clicked =
            ImGui::Button(m_slot_labels[slot].c_str(), ImVec2(cell, line * 3.2f));
        ImGui::PopStyleVar();
        if (clicked)
        {
          const auto& function = save ? m_actions.save_state : m_actions.load_state;
          if (function)
            action([function, slot] { function(slot); }, true);
        }
      }
      ImGui::Spacing();
      if (ImGui::Button("Back", full))
        m_menu = Menu::Game;
      break;
    }

    case Menu::ChangeDisc:
    {
      ImGui::TextUnformatted("Change Disc");
      if (m_discs.empty())
        ImGui::TextWrapped("No disc images in the game folders.");
      for (const std::string& path : m_discs)
      {
        std::string name;
        SplitPath(path, nullptr, &name, nullptr);
        if (ImGui::Button((name + "##" + path).c_str(), full) && m_actions.change_disc)
          action([change = m_actions.change_disc, path] { change(path); }, true);
      }
      ImGui::Spacing();
      if (ImGui::Button("Back", full))
        m_menu = Menu::Game;
      break;
    }

    default:
      break;
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(6);
  ImGui::PopFont();
}

// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/TouchOverlay.h"

#include <algorithm>
#include <cmath>

#include <QEventPoint>
#include <QString>

#include <imgui.h>
#include <imgui_internal.h>

#include "DolphinUT/Emulator.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/VideoEvents.h"

using ciface::Touch::ControlID;

TouchOverlay::TouchOverlay(Layout layout, bool sideways_option, Actions actions)
    : m_layout(layout), m_actions(std::move(actions))
{
  switch (layout)
  {
  case Layout::GameCube:
    BuildGameCube();
    break;
  case Layout::WiiRemoteSideways:
    BuildWiiRemoteSideways(sideways_option);
    break;
  case Layout::WiiRemoteNunchuk:
    BuildWiiRemoteNunchuk();
    break;
  }
  AddActions();

  if (layout == Layout::GameCube)
    ciface::Touch::RegisterGameCubeInputOverrider(m_controller_index);
  else
    ciface::Touch::RegisterWiiInputOverrider(m_controller_index);

  m_draw_hook = GetVideoEvents().before_present_event.Register([this](PresentInfo&) { Draw(); });
}

TouchOverlay::~TouchOverlay()
{
  m_draw_hook.reset();
  if (m_layout == Layout::GameCube)
    ciface::Touch::UnregisterGameCubeInputOverrider(m_controller_index);
  else
    ciface::Touch::UnregisterWiiInputOverrider(m_controller_index);
}

void TouchOverlay::AddButton(const char* label, float x, float y, float radius, ControlID id)
{
  auto control = std::make_unique<Control>();
  control->kind = Control::Kind::Button;
  control->label = label;
  control->x = x;
  control->y = y;
  control->radius = radius;
  control->id = id;
  m_controls.push_back(std::move(control));
}

void TouchOverlay::AddStick(const char* label, float x, float y, float radius, ControlID id_x,
                            ControlID id_y)
{
  AddButton(label, x, y, radius, id_x);
  m_controls.back()->kind = Control::Kind::Stick;
  m_controls.back()->id_y = id_y;
}

// Rough positions after the Android app's landscape GameCube layout.
void TouchOverlay::BuildGameCube()
{
  AddStick("", 0.13f, 0.66f, 0.17f, ControlID::GCPAD_MAIN_STICK_X, ControlID::GCPAD_MAIN_STICK_Y);
  AddStick("C", 0.70f, 0.82f, 0.10f, ControlID::GCPAD_C_STICK_X, ControlID::GCPAD_C_STICK_Y);
  AddButton("A", 0.88f, 0.66f, 0.10f, ControlID::GCPAD_A_BUTTON);
  AddButton("B", 0.78f, 0.80f, 0.065f, ControlID::GCPAD_B_BUTTON);
  AddButton("X", 0.955f, 0.50f, 0.065f, ControlID::GCPAD_X_BUTTON);
  AddButton("Y", 0.83f, 0.46f, 0.065f, ControlID::GCPAD_Y_BUTTON);
  AddButton("Z", 0.93f, 0.30f, 0.055f, ControlID::GCPAD_Z_BUTTON);
  AddButton("L", 0.07f, 0.28f, 0.065f, ControlID::GCPAD_L_DIGITAL);
  AddButton("R", 0.93f, 0.13f, 0.065f, ControlID::GCPAD_R_DIGITAL);
  AddButton("Start", 0.50f, 0.88f, 0.055f, ControlID::GCPAD_START_BUTTON);
  AddButton("^", 0.31f, 0.74f, 0.05f, ControlID::GCPAD_DPAD_UP);
  AddButton("v", 0.31f, 0.94f, 0.05f, ControlID::GCPAD_DPAD_DOWN);
  AddButton("<", 0.265f, 0.84f, 0.05f, ControlID::GCPAD_DPAD_LEFT);
  AddButton(">", 0.355f, 0.84f, 0.05f, ControlID::GCPAD_DPAD_RIGHT);
}

void TouchOverlay::AddAction(const char* label, float x, float y, std::function<void()> action)
{
  if (!action)
    return;
  auto control = std::make_unique<Control>();
  control->kind = Control::Kind::Action;
  control->label = label;
  control->x = x;
  control->y = y;
  control->radius = 0.05f;
  control->action = std::move(action);
  m_controls.push_back(std::move(control));
}

// Top left, in a row: Exit (back to the launcher), and for now Save / Load (state slot 1), which
// give tests a repeatable starting point. 3.4 replaces these with the in-game menu.
void TouchOverlay::AddActions()
{
  AddAction("Exit", 0.04f, 0.07f, m_actions.exit);
  AddAction("Save", 0.11f, 0.07f, m_actions.save_state);
  AddAction("Load", 0.18f, 0.07f, m_actions.load_state);
}

// A Wii Remote with no extension, held sideways (D-pad on the left), as games like New Super Mario
// Bros. Wii expect. Held that way, the player's left is the remote's up, and the player's up is the
// remote's right. With the "Sideways Wii Remote" option on, Dolphin does that rotation itself (and
// the Android overlay relies on it), so the D-pad is passed through unchanged.
void TouchOverlay::BuildWiiRemoteSideways(bool sideways_option)
{
  AddButton("^", 0.14f, 0.56f, 0.07f,
            sideways_option ? ControlID::WIIMOTE_DPAD_UP : ControlID::WIIMOTE_DPAD_RIGHT);
  AddButton("v", 0.14f, 0.84f, 0.07f,
            sideways_option ? ControlID::WIIMOTE_DPAD_DOWN : ControlID::WIIMOTE_DPAD_LEFT);
  AddButton("<", 0.07f, 0.70f, 0.07f,
            sideways_option ? ControlID::WIIMOTE_DPAD_LEFT : ControlID::WIIMOTE_DPAD_UP);
  AddButton(">", 0.21f, 0.70f, 0.07f,
            sideways_option ? ControlID::WIIMOTE_DPAD_RIGHT : ControlID::WIIMOTE_DPAD_DOWN);
  AddButton("1", 0.80f, 0.76f, 0.085f, ControlID::WIIMOTE_ONE_BUTTON);
  AddButton("2", 0.92f, 0.62f, 0.085f, ControlID::WIIMOTE_TWO_BUTTON);
  AddButton("A", 0.88f, 0.36f, 0.065f, ControlID::WIIMOTE_A_BUTTON);
  AddButton("B", 0.76f, 0.46f, 0.065f, ControlID::WIIMOTE_B_BUTTON);
  AddButton("-", 0.43f, 0.90f, 0.045f, ControlID::WIIMOTE_MINUS_BUTTON);
  AddButton("+", 0.57f, 0.90f, 0.045f, ControlID::WIIMOTE_PLUS_BUTTON);
  AddButton("Home", 0.50f, 0.08f, 0.045f, ControlID::WIIMOTE_HOME_BUTTON);
}

// A Wii Remote held upright with a Nunchuk: the Nunchuk's stick, C and Z on the left; the remote's
// D-pad and buttons on the right. Upright, the D-pad needs no rotation.
void TouchOverlay::BuildWiiRemoteNunchuk()
{
  AddStick("", 0.13f, 0.68f, 0.17f, ControlID::NUNCHUK_STICK_X, ControlID::NUNCHUK_STICK_Y);
  AddButton("C", 0.05f, 0.32f, 0.06f, ControlID::NUNCHUK_C_BUTTON);
  AddButton("Z", 0.13f, 0.32f, 0.065f, ControlID::NUNCHUK_Z_BUTTON);
  AddButton("A", 0.88f, 0.62f, 0.09f, ControlID::WIIMOTE_A_BUTTON);
  AddButton("B", 0.93f, 0.36f, 0.065f, ControlID::WIIMOTE_B_BUTTON);
  AddButton("^", 0.70f, 0.70f, 0.05f, ControlID::WIIMOTE_DPAD_UP);
  AddButton("v", 0.70f, 0.90f, 0.05f, ControlID::WIIMOTE_DPAD_DOWN);
  AddButton("<", 0.655f, 0.80f, 0.05f, ControlID::WIIMOTE_DPAD_LEFT);
  AddButton(">", 0.745f, 0.80f, 0.05f, ControlID::WIIMOTE_DPAD_RIGHT);
  AddButton("1", 0.85f, 0.90f, 0.05f, ControlID::WIIMOTE_ONE_BUTTON);
  AddButton("2", 0.94f, 0.90f, 0.05f, ControlID::WIIMOTE_TWO_BUTTON);
  AddButton("-", 0.43f, 0.90f, 0.045f, ControlID::WIIMOTE_MINUS_BUTTON);
  AddButton("+", 0.57f, 0.90f, 0.045f, ControlID::WIIMOTE_PLUS_BUTTON);
  AddButton("Home", 0.50f, 0.08f, 0.045f, ControlID::WIIMOTE_HOME_BUTTON);
}

TouchOverlay::Control* TouchOverlay::HitTest(float x, float y, QSizeF window_size) const
{
  const float unit = static_cast<float>(std::min(window_size.width(), window_size.height()));
  Control* best = nullptr;
  float best_distance = 0.0f;
  for (const auto& control : m_controls)
  {
    const float dx = x - control->x * static_cast<float>(window_size.width());
    const float dy = y - control->y * static_cast<float>(window_size.height());
    const float distance = std::sqrt(dx * dx + dy * dy);
    // A little slack around each control, as fingers are not precise.
    if (distance <= control->radius * unit * 1.25f && (!best || distance < best_distance))
    {
      best = control.get();
      best_distance = distance;
    }
  }
  return best;
}

void TouchOverlay::Press(Control* control, float x, float y, QSizeF window_size)
{
  switch (control->kind)
  {
  case Control::Kind::Button:
    control->pressed = true;
    ciface::Touch::SetControlState(m_controller_index, control->id, 1.0);
    break;
  case Control::Kind::Stick:
  {
    const float unit = static_cast<float>(std::min(window_size.width(), window_size.height()));
    float sx = (x - control->x * static_cast<float>(window_size.width())) / (control->radius * unit);
    float sy =
        (y - control->y * static_cast<float>(window_size.height())) / (control->radius * unit);
    const float length = std::sqrt(sx * sx + sy * sy);
    if (length > 1.0f)
    {
      sx /= length;
      sy /= length;
    }
    control->pressed = true;
    control->stick_x = sx;
    control->stick_y = sy;
    ciface::Touch::SetControlState(m_controller_index, control->id, sx);
    ciface::Touch::SetControlState(m_controller_index, control->id_y, -sy);
    break;
  }
  case Control::Kind::Action:
    control->pressed = true;
    break;
  }
}

void TouchOverlay::Release(Control* control)
{
  control->pressed = false;
  switch (control->kind)
  {
  case Control::Kind::Button:
    ciface::Touch::SetControlState(m_controller_index, control->id, 0.0);
    break;
  case Control::Kind::Stick:
    control->stick_x = 0.0f;
    control->stick_y = 0.0f;
    ciface::Touch::SetControlState(m_controller_index, control->id, 0.0);
    ciface::Touch::SetControlState(m_controller_index, control->id_y, 0.0);
    break;
  case Control::Kind::Action:
    control->pressed = false;
    if (control->action)
      control->action();
    break;
  }
}

void TouchOverlay::HandleTouch(const QList<QEventPoint>& points, QSizeF window_size,
                               qreal device_pixel_ratio, bool cancel)
{
  for (const QEventPoint& point : points)
  {
    // Touch positions are window-relative logical pixels; the controls are laid out in physical
    // ones.
    const float x = static_cast<float>(point.position().x() * device_pixel_ratio);
    const float y = static_cast<float>(point.position().y() * device_pixel_ratio);

    auto owner = m_touches.find(point.id());
    if (cancel || point.state() == QEventPoint::State::Released)
    {
      if (owner != m_touches.end())
      {
        if (!cancel || owner->second->kind != Control::Kind::Action)
          Release(owner->second);
        else
          owner->second->pressed = false;
        m_touches.erase(owner);
      }
      continue;
    }

    if (owner == m_touches.end())
    {
      if (point.state() != QEventPoint::State::Pressed)
        continue;
      Control* control = HitTest(x, y, window_size);
      if (!control)
        continue;
      m_touches[point.id()] = control;
      Press(control, x, y, window_size);
    }
    else if (owner->second->kind == Control::Kind::Stick)
    {
      Press(owner->second, x, y, window_size);
    }
  }
}

void TouchOverlay::Draw()
{
  ImGuiContext* context = ImGui::GetCurrentContext();
  if (!context || !context->WithinFrameScope)
    return;

  // before_present_event also fires for presents that end early without drawing and resetting the
  // ImGui frame (seen with Vulkan: Presenter::Present returns before Finalize/BeginImGuiFrame when
  // the backend is headless). Drawing again into the same frame then piles up vertices until the
  // Vulkan stream buffer overflows (VKStreamBuffer::CommitMemory assert). Draw once per frame.
  const int frame = ImGui::GetFrameCount();
  if (frame == m_last_drawn_frame)
    return;
  m_last_drawn_frame = frame;

  const auto start = std::chrono::steady_clock::now();
  ImDrawList* draw_list = ImGui::GetForegroundDrawList();
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float unit = std::min(display.x, display.y);
  const ImU32 outline = IM_COL32(255, 255, 255, 110);
  const ImU32 fill = IM_COL32(255, 255, 255, 40);
  const ImU32 fill_pressed = IM_COL32(255, 255, 255, 120);
  const ImU32 text = IM_COL32(255, 255, 255, 200);

  for (const auto& control : m_controls)
  {
    const ImVec2 center(control->x * display.x, control->y * display.y);
    const float radius = control->radius * unit;
    const bool pressed = control->pressed;
    draw_list->AddCircleFilled(center, radius, pressed ? fill_pressed : fill, 48);
    draw_list->AddCircle(center, radius, outline, 48, std::max(2.0f, unit * 0.004f));

    if (control->kind == Control::Kind::Stick)
    {
      const ImVec2 knob(center.x + control->stick_x * radius, center.y + control->stick_y * radius);
      draw_list->AddCircleFilled(knob, radius * 0.45f, pressed ? fill_pressed : outline, 32);
    }

    if (!control->label.empty())
    {
      // ImGui 1.92 rasterizes glyphs per requested size into a growing atlas. Large sizes made the
      // Vulkan backend hit an assert while uploading it (VKStreamBuffer::CommitMemory), so cap the
      // size; the real overlay will use the Android button images instead of text (3.3).
      const float font_size =
          std::clamp(radius * (control->label.size() > 1 ? 0.55f : 0.9f), 14.0f, 40.0f);
      const ImVec2 size =
          ImGui::GetFont()->CalcTextSizeA(font_size, 1e9f, 0.0f, control->label.c_str());
      draw_list->AddText(ImGui::GetFont(), font_size,
                         ImVec2(center.x - size.x / 2, center.y - size.y / 2), text,
                         control->label.c_str());
    }
  }

  m_draw_time += std::chrono::steady_clock::now() - start;
  if (++m_draw_count == 600)
  {
    Emulator::LogFromAnyThread(
        QStringLiteral("overlay: %1 us per frame (CPU, video thread), display %2x%3")
            .arg(std::chrono::duration<double, std::micro>(m_draw_time).count() / m_draw_count,
                 0, 'f', 1)
            .arg(display.x)
            .arg(display.y));
    m_draw_time = {};
    m_draw_count = 0;
  }
}

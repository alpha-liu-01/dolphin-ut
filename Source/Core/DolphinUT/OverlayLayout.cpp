// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/OverlayLayout.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string_view>

#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/WiimoteSettings.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/ExtensionPort.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "InputCommon/ControllerEmu/ControlGroup/Attachments.h"
#include "InputCommon/ControllerEmu/Setting/NumericSetting.h"

using ciface::Touch::ControlID;

namespace OverlayLayout
{
namespace
{
struct OverlayDefaultPosition
{
  const char* button;
  const char* variant;
  bool portrait;
  int x;
  int y;
};

struct OverlayButtonSetting
{
  enum Kind
  {
    Toggle,
    Latching,
  };
  Kind kind;
  const char* group;
  int index;
  const char* key;
  bool default_value;
};

#include "DolphinUT/OverlayDefaults.inc"

constexpr ControlID NONE = ControlID::NUMBER_OF_CONTROLS;

using Kind = ControlDef::Kind;

const Config::Info<bool> SHOW_INPUT_OVERLAY{{Config::System::Main, "Android", "ShowInputOverlay"},
                                            true};
const Config::Info<bool> MENU_BUTTON_AUTO_HIDE{
    {Config::System::Main, "UbuntuTouch", "MenuButtonAutoHide"}, true};
const Config::Info<int> CONTROL_SCALE{{Config::System::Main, "Android", "ControlScale"}, 50};
const Config::Info<int> CONTROL_OPACITY{{Config::System::Main, "Android", "ControlOpacity"}, 65};
const Config::Info<bool> JOYSTICK_REL_CENTER{{Config::System::Main, "Android", "JoystickRelCenter"},
                                             true};
const Config::Info<int> IR_MODE{{Config::System::Main, "Android", "IRMode"}, 1};
const Config::Info<bool> IR_ALWAYS_RECENTER{{Config::System::Main, "Android", "IRAlwaysRecenter"},
                                            false};
const Config::Info<int> DOUBLE_TAP_BUTTON{
    {Config::System::Main, "AndroidOverlayButtons", "DoubleTapButton"}, 100};
const Config::Info<int> OVERLAY_GC_CONTROLLER{
    {Config::System::Main, "Android", "OverlayGCController"}, 0};
const Config::Info<int> OVERLAY_WII_CONTROLLER{
    {Config::System::Main, "Android", "OverlayWiiController"}, 4};

std::optional<Config::Info<bool>> ButtonSettingInfo(OverlayButtonSetting::Kind kind,
                                                    const ControlDef& control)
{
  for (const OverlayButtonSetting& setting : s_button_settings)
  {
    if (setting.kind == kind && setting.index == control.index &&
        std::strcmp(setting.group, control.group) == 0)
    {
      return std::optional<Config::Info<bool>>(
          std::in_place,
          Config::Location{Config::System::Main, "AndroidOverlayButtons", setting.key},
          setting.default_value);
    }
  }
  return std::nullopt;
}

bool ButtonSetting(OverlayButtonSetting::Kind kind, const ControlDef& control)
{
  const auto info = ButtonSettingInfo(kind, control);
  return info ? Config::Get(*info) : kind == OverlayButtonSetting::Toggle;
}

template <typename T>
void SetAndSave(const Config::Info<T>& info, const T& value)
{
  Config::SetBase(info, value);
  Config::Save();
}

std::string LayoutFile()
{
  return File::GetUserPath(D_CONFIG_IDX) + "TouchOverlay.ini";
}

// The Android app's position key: <ButtonType>[_H|_O][-Portrait].
std::string PositionKey(Type type, const char* button_type, bool portrait)
{
  std::string key = button_type;
  const char* variant = PositionVariant(type, button_type);
  if (*variant)
    key += std::string("_") + variant;
  if (portrait)
    key += "-Portrait";
  return key;
}
}  // namespace

std::vector<ControlDef> GetControls(Type type)
{
  // InputOverlay.add*OverlayControls, with the sizes of initializeOverlayButton/Dpad/Joystick.
  // Images: button = {default, pressed}; D-pad = {default, one direction, two directions};
  // joystick = {range, inner, inner pressed}.
  const auto button = [](const char* button_type, const char* image, const char* pressed,
                         float size, ControlID control, const char* group, int index) {
    return ControlDef{Kind::Button, button_type,       {image, pressed, nullptr},
                      size,         {control, NONE, NONE, NONE}, group, index};
  };
  const auto dpad = [](const char* button_type, float size, ControlID up, ControlID down,
                       ControlID left, ControlID right, const char* group, int index) {
    return ControlDef{Kind::Dpad,
                      button_type,
                      {"gcwii_dpad", "gcwii_dpad_pressed_one_direction",
                       "gcwii_dpad_pressed_two_directions"},
                      size,
                      {up, down, left, right},
                      group,
                      index};
  };
  const auto joystick = [](const char* button_type, const char* inner, const char* inner_pressed,
                           ControlID x, ControlID y, const char* group, int index) {
    return ControlDef{Kind::Joystick, button_type, {"gcwii_joystick_range", inner, inner_pressed},
                      0.275f,         {x, y, NONE, NONE}, group, index};
  };

  std::vector<ControlDef> controls;
  switch (type)
  {
  case Type::GameCube:
    controls = {
        button("BUTTON_A", "gcpad_a", "gcpad_a_pressed", 0.2f, ControlID::GCPAD_A_BUTTON, "GC", 0),
        button("BUTTON_B", "gcpad_b", "gcpad_b_pressed", 0.125f, ControlID::GCPAD_B_BUTTON, "GC",
               1),
        button("BUTTON_X", "gcpad_x", "gcpad_x_pressed", 0.175f, ControlID::GCPAD_X_BUTTON, "GC",
               2),
        button("BUTTON_Y", "gcpad_y", "gcpad_y_pressed", 0.175f, ControlID::GCPAD_Y_BUTTON, "GC",
               3),
        button("BUTTON_Z", "gcpad_z", "gcpad_z_pressed", 0.225f, ControlID::GCPAD_Z_BUTTON, "GC",
               4),
        button("BUTTON_START", "gcpad_start", "gcpad_start_pressed", 0.075f,
               ControlID::GCPAD_START_BUTTON, "GC", 5),
        button("TRIGGER_L", "gcpad_l", "gcpad_l_pressed", 0.225f, ControlID::GCPAD_L_DIGITAL, "GC",
               6),
        button("TRIGGER_R", "gcpad_r", "gcpad_r_pressed", 0.225f, ControlID::GCPAD_R_DIGITAL, "GC",
               7),
        dpad("BUTTON_UP", 0.2375f, ControlID::GCPAD_DPAD_UP, ControlID::GCPAD_DPAD_DOWN,
             ControlID::GCPAD_DPAD_LEFT, ControlID::GCPAD_DPAD_RIGHT, "GC", 8),
        joystick("STICK_MAIN", "gcwii_joystick", "gcwii_joystick_pressed",
                 ControlID::GCPAD_MAIN_STICK_X, ControlID::GCPAD_MAIN_STICK_Y, "GC", 9),
        joystick("STICK_C", "gcpad_c", "gcpad_c_pressed", ControlID::GCPAD_C_STICK_X,
                 ControlID::GCPAD_C_STICK_Y, "GC", 10),
    };
    break;

  case Type::WiiRemote:
  case Type::WiiRemoteSideways:
  case Type::WiiRemoteNunchuk:
  {
    const bool sideways = type == Type::WiiRemoteSideways;
    const bool bare = type != Type::WiiRemoteNunchuk;
    const float one_two = sideways ? 0.14f : 0.0875f;
    controls = {
        button("WIIMOTE_BUTTON_A", "wiimote_a", "wiimote_a_pressed", 0.125f,
               ControlID::WIIMOTE_A_BUTTON, "WII", 0),
        button("WIIMOTE_BUTTON_B", "wiimote_b", "wiimote_b_pressed", 0.2f,
               ControlID::WIIMOTE_B_BUTTON, "WII", 1),
        button("WIIMOTE_BUTTON_1", "wiimote_one", "wiimote_one_pressed", one_two,
               ControlID::WIIMOTE_ONE_BUTTON, "WII", 2),
        button("WIIMOTE_BUTTON_2", "wiimote_two", "wiimote_two_pressed", one_two,
               ControlID::WIIMOTE_TWO_BUTTON, "WII", 3),
        button("WIIMOTE_BUTTON_PLUS", "wiimote_plus", "wiimote_plus_pressed", 0.0625f,
               ControlID::WIIMOTE_PLUS_BUTTON, "WII", 4),
        button("WIIMOTE_BUTTON_MINUS", "wiimote_minus", "wiimote_minus_pressed", 0.0625f,
               ControlID::WIIMOTE_MINUS_BUTTON, "WII", 5),
        button("WIIMOTE_BUTTON_HOME", "wiimote_home", "wiimote_home_pressed", 0.0625f,
               ControlID::WIIMOTE_HOME_BUTTON, "WII", 6),
        dpad("WIIMOTE_UP", bare ? 0.275f : 0.2125f, ControlID::WIIMOTE_DPAD_UP,
             ControlID::WIIMOTE_DPAD_DOWN, ControlID::WIIMOTE_DPAD_LEFT,
             ControlID::WIIMOTE_DPAD_RIGHT, "WII", 7),
    };
    if (type == Type::WiiRemoteNunchuk)
    {
      controls.push_back(button("NUNCHUK_BUTTON_C", "nunchuk_c", "nunchuk_c_pressed", 0.125f,
                                ControlID::NUNCHUK_C_BUTTON, "WII", 8));
      controls.push_back(button("NUNCHUK_BUTTON_Z", "nunchuk_z", "nunchuk_z_pressed", 0.2f,
                                ControlID::NUNCHUK_Z_BUTTON, "WII", 9));
      controls.push_back(joystick("NUNCHUK_STICK", "gcwii_joystick", "gcwii_joystick_pressed",
                                  ControlID::NUNCHUK_STICK_X, ControlID::NUNCHUK_STICK_Y, "WII",
                                  10));
    }
    break;
  }

  case Type::WiiRemoteClassic:
    controls = {
        button("CLASSIC_BUTTON_A", "classic_a", "classic_a_pressed", 0.125f,
               ControlID::CLASSIC_A_BUTTON, "CLASSIC", 0),
        button("CLASSIC_BUTTON_B", "classic_b", "classic_b_pressed", 0.125f,
               ControlID::CLASSIC_B_BUTTON, "CLASSIC", 1),
        button("CLASSIC_BUTTON_X", "classic_x", "classic_x_pressed", 0.125f,
               ControlID::CLASSIC_X_BUTTON, "CLASSIC", 2),
        button("CLASSIC_BUTTON_Y", "classic_y", "classic_y_pressed", 0.125f,
               ControlID::CLASSIC_Y_BUTTON, "CLASSIC", 3),
        button("CLASSIC_BUTTON_PLUS", "wiimote_plus", "wiimote_plus_pressed", 0.0625f,
               ControlID::CLASSIC_PLUS_BUTTON, "CLASSIC", 4),
        button("CLASSIC_BUTTON_MINUS", "wiimote_minus", "wiimote_minus_pressed", 0.0625f,
               ControlID::CLASSIC_MINUS_BUTTON, "CLASSIC", 5),
        button("CLASSIC_BUTTON_HOME", "wiimote_home", "wiimote_home_pressed", 0.0625f,
               ControlID::CLASSIC_HOME_BUTTON, "CLASSIC", 6),
        button("CLASSIC_TRIGGER_L", "classic_l", "classic_l_pressed", 0.25f,
               ControlID::CLASSIC_L_DIGITAL, "CLASSIC", 7),
        button("CLASSIC_TRIGGER_R", "classic_r", "classic_r_pressed", 0.25f,
               ControlID::CLASSIC_R_DIGITAL, "CLASSIC", 8),
        button("CLASSIC_BUTTON_ZL", "classic_zl", "classic_zl_pressed", 0.25f,
               ControlID::CLASSIC_ZL_BUTTON, "CLASSIC", 9),
        button("CLASSIC_BUTTON_ZR", "classic_zr", "classic_zr_pressed", 0.25f,
               ControlID::CLASSIC_ZR_BUTTON, "CLASSIC", 10),
        dpad("CLASSIC_DPAD_UP", 0.275f, ControlID::CLASSIC_DPAD_UP, ControlID::CLASSIC_DPAD_DOWN,
             ControlID::CLASSIC_DPAD_LEFT, ControlID::CLASSIC_DPAD_RIGHT, "CLASSIC", 11),
        joystick("CLASSIC_STICK_LEFT", "gcwii_joystick", "gcwii_joystick_pressed",
                 ControlID::CLASSIC_LEFT_STICK_X, ControlID::CLASSIC_LEFT_STICK_Y, "CLASSIC", 12),
        joystick("CLASSIC_STICK_RIGHT", "gcwii_joystick", "gcwii_joystick_pressed",
                 ControlID::CLASSIC_RIGHT_STICK_X, ControlID::CLASSIC_RIGHT_STICK_Y, "CLASSIC",
                 13),
    };
    break;

  case Type::None:
    break;
  }
  return controls;
}

const char* PositionVariant(Type type, const char* button_type)
{
  const std::string_view name(button_type);
  if (type == Type::WiiRemoteSideways &&
      (name == "WIIMOTE_BUTTON_A" || name == "WIIMOTE_BUTTON_B" || name == "WIIMOTE_BUTTON_1" ||
       name == "WIIMOTE_BUTTON_2"))
  {
    return "H";
  }
  if (type == Type::WiiRemote && name == "WIIMOTE_UP")
    return "O";
  return "";
}

std::optional<std::pair<float, float>> DefaultPosition(Type type, const char* button_type,
                                                       bool portrait)
{
  const char* variant = PositionVariant(type, button_type);
  for (const OverlayDefaultPosition& position : s_default_positions)
  {
    if (position.portrait == portrait && std::strcmp(position.button, button_type) == 0 &&
        std::strcmp(position.variant, variant) == 0)
    {
      return std::make_pair(position.x / 1000.0f, position.y / 1000.0f);
    }
  }
  return std::nullopt;
}

float JoystickInnerScale(const char* button_type)
{
  return std::strcmp(button_type, "STICK_C") == 0 ? 1.833f : 1.375f;
}

std::optional<ControlID> AnalogForTrigger(ControlID control)
{
  switch (control)
  {
  case ControlID::GCPAD_L_DIGITAL:
    return ControlID::GCPAD_L_ANALOG;
  case ControlID::GCPAD_R_DIGITAL:
    return ControlID::GCPAD_R_ANALOG;
  case ControlID::CLASSIC_L_DIGITAL:
    return ControlID::CLASSIC_L_ANALOG;
  case ControlID::CLASSIC_R_DIGITAL:
    return ControlID::CLASSIC_R_ANALOG;
  default:
    return std::nullopt;
  }
}

bool ShowOverlay()
{
  return Config::Get(SHOW_INPUT_OVERLAY);
}
bool MenuButtonAutoHide()
{
  return Config::Get(MENU_BUTTON_AUTO_HIDE);
}
int ControlScale()
{
  return Config::Get(CONTROL_SCALE);
}
int ControlOpacity()
{
  return Config::Get(CONTROL_OPACITY);
}
bool JoystickRelativeCenter()
{
  return Config::Get(JOYSTICK_REL_CENTER);
}
int IRMode()
{
  return Config::Get(IR_MODE);
}
bool IRAlwaysRecenter()
{
  return Config::Get(IR_ALWAYS_RECENTER);
}
int DoubleTapButton()
{
  return Config::Get(DOUBLE_TAP_BUTTON);
}
int OverlayGCController()
{
  return Config::Get(OVERLAY_GC_CONTROLLER);
}
int OverlayWiiController()
{
  return Config::Get(OVERLAY_WII_CONTROLLER);
}
bool ControlShown(const ControlDef& control)
{
  return ButtonSetting(OverlayButtonSetting::Toggle, control);
}
bool ControlLatching(const ControlDef& control)
{
  return ButtonSetting(OverlayButtonSetting::Latching, control);
}

int ControllerIndex(bool wii)
{
  const int value = wii ? OverlayWiiController() : OverlayGCController();
  return value >= 4 ? value - 4 : value;
}

Type ConfiguredType(bool wii)
{
  const int value = wii ? OverlayWiiController() : OverlayGCController();
  if (value >= 0 && value < 4)
  {
    // A GameCube port (also in Wii games, for games that use GameCube controllers).
    const SerialInterface::SIDevices device = Config::Get(Config::GetInfoForSIDevice(value));
    return device == SerialInterface::SIDEVICE_GC_CONTROLLER ||
                   device == SerialInterface::SIDEVICE_AM_BASEBOARD ?
               Type::GameCube :
               Type::None;
  }
  if (value < 4 || value > 7)
    return Type::None;
  const int index = value - 4;

  if (Config::Get(Config::GetInfoForWiimoteSource(index)) != WiimoteSource::Emulated)
    return Type::None;

  const auto* attachments = static_cast<ControllerEmu::Attachments*>(
      Wiimote::GetWiimoteGroup(index, WiimoteEmu::WiimoteGroup::Attachments));
  switch (attachments->GetSelectedAttachment())
  {
  case WiimoteEmu::ExtensionNumber::NUNCHUK:
    return Type::WiiRemoteNunchuk;
  case WiimoteEmu::ExtensionNumber::CLASSIC:
    return Type::WiiRemoteClassic;
  default:
    break;
  }

  for (auto& setting : Wiimote::GetWiimoteGroup(index, WiimoteEmu::WiimoteGroup::Options)
                           ->numeric_settings)
  {
    if (setting->GetININame() == WiimoteEmu::Wiimote::SIDEWAYS_OPTION &&
        static_cast<ControllerEmu::NumericSetting<bool>*>(setting.get())->GetValue())
    {
      return Type::WiiRemoteSideways;
    }
  }
  return Type::WiiRemote;
}

const char* Label(const ControlDef& control)
{
  // arrays.xml: gcpadButtons, nunchukButtons (wiimoteButtons is its first 8), classicButtons.
  static constexpr const char* gc[] = {"A", "B", "X", "Y", "Z", "Start", "L", "R",
                                       "D-Pad", "Main Stick", "C Stick"};
  static constexpr const char* wii[] = {"A", "B", "1", "2", "+", "-", "Home", "D-Pad",
                                        "C", "Z", "Nunchuk Stick"};
  static constexpr const char* classic[] = {"a",  "b",  "x",  "y",     "+",          "-",
                                            "Home", "L", "R", "ZL",   "ZR",         "D-Pad",
                                            "Left Stick", "Right Stick"};
  const std::string_view group(control.group);
  const int i = control.index;
  if (group == "GC" && i >= 0 && i < static_cast<int>(std::size(gc)))
    return gc[i];
  if (group == "WII" && i >= 0 && i < static_cast<int>(std::size(wii)))
    return wii[i];
  if (group == "CLASSIC" && i >= 0 && i < static_cast<int>(std::size(classic)))
    return classic[i];
  return control.button_type;
}

std::vector<std::pair<int, std::string>> ControllerChoices(bool wii)
{
  std::vector<std::pair<int, std::string>> choices{{-1, "None"}};
  for (int i = 0; i < 4; ++i)
  {
    if (Config::Get(Config::GetInfoForSIDevice(i)) != SerialInterface::SIDEVICE_NONE)
      choices.emplace_back(i, "GameCube Controller " + std::to_string(i + 1));
  }
  if (wii)
  {
    for (int i = 0; i < 4; ++i)
    {
      if (Config::Get(Config::GetInfoForWiimoteSource(i)) != WiimoteSource::None)
        choices.emplace_back(i + 4, "Wii Remote " + std::to_string(i + 1));
    }
  }
  return choices;
}

void SetShowOverlay(bool show)
{
  SetAndSave(SHOW_INPUT_OVERLAY, show);
}
void SetMenuButtonAutoHide(bool auto_hide)
{
  SetAndSave(MENU_BUTTON_AUTO_HIDE, auto_hide);
}
void SetControlScale(int scale)
{
  SetAndSave(CONTROL_SCALE, std::clamp(scale, 0, 150));
}
void SetControlOpacity(int opacity)
{
  SetAndSave(CONTROL_OPACITY, std::clamp(opacity, 0, 100));
}
void ResetScaleAndOpacity()
{
  // As the Android app's "Default Values": the settings go back to their defaults.
  Config::DeleteKey(Config::LayerType::Base, CONTROL_SCALE);
  Config::DeleteKey(Config::LayerType::Base, CONTROL_OPACITY);
  Config::Save();
}
void SetJoystickRelativeCenter(bool relative)
{
  SetAndSave(JOYSTICK_REL_CENTER, relative);
}
void SetIRMode(int mode)
{
  SetAndSave(IR_MODE, std::clamp(mode, 0, 2));
}
void SetIRAlwaysRecenter(bool recenter)
{
  SetAndSave(IR_ALWAYS_RECENTER, recenter);
}
void SetDoubleTapButton(int button_type)
{
  SetAndSave(DOUBLE_TAP_BUTTON, button_type);
}
void SetOverlayController(bool wii, int value)
{
  SetAndSave(wii ? OVERLAY_WII_CONTROLLER : OVERLAY_GC_CONTROLLER, value);
}
void SetControlShown(const ControlDef& control, bool shown)
{
  if (const auto info = ButtonSettingInfo(OverlayButtonSetting::Toggle, control))
    SetAndSave(*info, shown);
}
void SetControlLatching(const ControlDef& control, bool latching)
{
  if (const auto info = ButtonSettingInfo(OverlayButtonSetting::Latching, control))
    SetAndSave(*info, latching);
}

std::optional<std::pair<float, float>> SavedPosition(Type type, const char* button_type,
                                                     bool portrait)
{
  Common::IniFile ini;
  ini.Load(LayoutFile());
  const Common::IniFile::Section* section = ini.GetSection("Layout");
  if (!section)
    return std::nullopt;
  const std::string key = PositionKey(type, button_type, portrait);
  float x = 0.0f, y = 0.0f;
  if (!section->Get(key + "-X", &x) || !section->Get(key + "-Y", &y))
    return std::nullopt;
  return std::make_pair(x, y);
}

void SavePosition(Type type, const char* button_type, bool portrait, float x, float y)
{
  Common::IniFile ini;
  ini.Load(LayoutFile());
  Common::IniFile::Section* section = ini.GetOrCreateSection("Layout");
  const std::string key = PositionKey(type, button_type, portrait);
  section->Set(key + "-X", x);
  section->Set(key + "-Y", y);
  ini.Save(LayoutFile());
}

void ResetPositions(Type type, bool portrait)
{
  Common::IniFile ini;
  ini.Load(LayoutFile());
  Common::IniFile::Section* section = ini.GetOrCreateSection("Layout");
  for (const ControlDef& control : GetControls(type))
  {
    const std::string key = PositionKey(type, control.button_type, portrait);
    section->Delete(key + "-X");
    section->Delete(key + "-Y");
  }
  // Stage 7: the menu button goes back to its place too.
  const std::string menu_key = PositionKey(type, MENU_BUTTON, portrait);
  section->Delete(menu_key + "-X");
  section->Delete(menu_key + "-Y");
  ini.Save(LayoutFile());
}
}  // namespace OverlayLayout

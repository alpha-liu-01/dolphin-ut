// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "InputCommon/ControllerInterface/Touch/InputOverrider.h"

// The Android app's touch overlay layouts (stage 3.3, docs/ut-port/stage3.3-overlay.md): which
// controls each controller type has, their graphics and sizes (InputOverlay.kt), their default
// positions (res/values/integers.xml, through OverlayDefaults.inc), and the overlay settings, kept
// under the Android app's own sections and keys in Dolphin.ini so the two read the same settings.
namespace OverlayLayout
{
// InputOverlay.OVERLAY_*.
enum class Type
{
  GameCube,
  WiiRemote,          // upright, with the touch pointer
  WiiRemoteSideways,  // "Sideways Wii Remote" on
  WiiRemoteNunchuk,
  WiiRemoteClassic,
  None,
};

struct ControlDef
{
  enum class Kind
  {
    Button,
    Dpad,
    Joystick,
  };

  Kind kind;
  // The Android ButtonType name: the key of the default position and of the saved position.
  const char* button_type;
  // Images in the Android app's drawable-xxhdpi (bundled under :/overlay/).
  // Button: default, pressed. D-pad: default, one direction, two directions.
  // Joystick: range (outer), inner, inner pressed.
  const char* images[3];
  // Size of the image's longer side, as a fraction of the screen's shorter side, at the default
  // control scale (InputOverlay.initializeOverlay*).
  float size;
  // Button: [0]. D-pad: up, down, left, right. Joystick: x, y.
  ciface::Touch::ControlID controls[4];
  // [AndroidOverlayButtons] ButtonToggle… / ButtonLatching… : group ("GC", "WII", "CLASSIC") and
  // index, as BooleanSetting.MAIN_BUTTON_TOGGLE_<group>_<index>.
  const char* group;
  int index;
};

// The controls of a layout, in the Android app's drawing order.
std::vector<ControlDef> GetControls(Type type);

// The Android app's key variant for a control in a layout: "_H" (sideways) for the Wii Remote's
// A, B, 1, 2; "_O" (upright) for its D-pad; otherwise none.
const char* PositionVariant(Type type, const char* button_type);

// The default top-left corner as fractions of the window (x of its width, y of its height).
std::optional<std::pair<float, float>> DefaultPosition(Type type, const char* button_type,
                                                       bool portrait);

// The size of a joystick's inner part relative to its range (InputOverlay.initializeOverlayJoystick).
float JoystickInnerScale(const char* button_type);

// The analog control pressed together with a digital trigger (InputOverlay.getAnalogControlForTrigger).
std::optional<ciface::Touch::ControlID> AnalogForTrigger(ciface::Touch::ControlID control);

// Settings, as the Android app's (section [Android] and [AndroidOverlayButtons] of Dolphin.ini).
bool ShowOverlay();             // ShowInputOverlay, default on
// Stage 7: the in-game menu's button hides itself during play ([UbuntuTouch] MenuButtonAutoHide,
// default on); a tap outside the controls brings it back for a few seconds.
bool MenuButtonAutoHide();
// The menu button's key in TouchOverlay.ini's positions (one position for every layout, per
// orientation), moved in Edit Layout as the controls are.
constexpr const char* MENU_BUTTON = "UT_MENU_BUTTON";
int ControlScale();             // ControlScale, 0–150, default 50 (= 100 %)
int ControlOpacity();           // ControlOpacity, 0–100, default 65
bool JoystickRelativeCenter();  // JoystickRelCenter, default on
int IRMode();                   // IRMode: 0 off, 1 follow (default), 2 drag
bool IRAlwaysRecenter();        // IRAlwaysRecenter, default off
int DoubleTapButton();          // DoubleTapButton: Android ButtonType number, default 100 (Wii A)
int OverlayGCController();      // OverlayGCController: 0–3 GameCube ports, default 0
int OverlayWiiController();     // OverlayWiiController: 4–7 Wii Remotes, default 4
bool ControlShown(const ControlDef& control);
bool ControlLatching(const ControlDef& control);

// The Android app's name for a control in its menus (res/values/arrays.xml).
const char* Label(const ControlDef& control);

// Which layout the overlay shows, as InputOverlay.configuredControllerType: the controller chosen
// for GameCube or Wii games (OverlayGCController / OverlayWiiController: 0–3 GameCube ports, 4–7
// Wii Remotes, -1 none), if that port has an emulated controller.
Type ConfiguredType(bool wii);
// The emulated controller index (0–3) the overlay drives, of a GameCube port or a Wii Remote.
int ControllerIndex(bool wii);

// "Choose Controller" (EmulationActivity.chooseController): {setting value, name} for None and each
// GameCube port and (in Wii games) Wii Remote that has a controller.
std::vector<std::pair<int, std::string>> ControllerChoices(bool wii);

// Setters: the base layer, saved to Dolphin.ini at once (as the Android app's settings).
void SetShowOverlay(bool show);
void SetMenuButtonAutoHide(bool auto_hide);
void SetControlScale(int scale);
void SetControlOpacity(int opacity);
void ResetScaleAndOpacity();
void SetJoystickRelativeCenter(bool relative);
void SetIRMode(int mode);
void SetIRAlwaysRecenter(bool recenter);
void SetDoubleTapButton(int button_type);
void SetOverlayController(bool wii, int value);
void SetControlShown(const ControlDef& control, bool shown);
void SetControlLatching(const ControlDef& control, bool latching);

// Positions moved in edit mode: top-left corners as fractions of the window, per layout variant and
// orientation, in Config/TouchOverlay.ini (the Android app keeps them in its SharedPreferences).
std::optional<std::pair<float, float>> SavedPosition(Type type, const char* button_type,
                                                     bool portrait);
void SavePosition(Type type, const char* button_type, bool portrait, float x, float y);
// "Reset Overlay": the current layout's positions for one orientation back to the defaults.
void ResetPositions(Type type, bool portrait);
}  // namespace OverlayLayout

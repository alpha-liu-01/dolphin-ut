# Stage 7: release follow-ups (an extension of stage 5)

Added 2026-11-17 (the user): work around the first OpenStore release, some of it after it. The overview is in `roadmap.md`, stage 7.

| Item | When | Status |
| --- | --- | --- |
| The in-game menu button: hide during play, movable | before the release | built and checked over adb (below); on-screen tests pending |
| Translations of the port's strings | partly after the release | not started; measurement and options in `stage6.8-leftovers.md` |
| OpenStore submission's decisions (name, maintainer, title, screenshots, a pushed commit) | the release | the user's (stage 5) |
| RetroAchievements' on-screen tests | when a tester is found | deferred (6.7) |
| Whether the installed click leaves itself out of "Send to Another App" | with the next click build | open (6.5) |

## The in-game menu button

### The problem

The user's report: "The menu button is at a fairly annoying position and it can never be moved or hidden."
- It was a small round "Menu" button at the top centre, always drawn and always live (3.4).
- Edit Layout could move the controls but not it.
- The Android app has no such button (it opens its menu with the system Back), and Ubuntu Touch has no system Back.

### The design (proposed, and approved by the user on 2026-11-17)

- **Auto-hide, on by default.** The button shows for 3 s when a game starts, then fades out (0.4 s).
  - A tap anywhere outside the touch controls brings it back for 3 s.
  - Pressing it, an open menu, or a paused game keeps it shown.
- **Hidden means not pressable.** A tap where the hidden button is only brings it back, so it cannot be hit by accident mid-game. The next tap presses it.
- **Movable:** Edit Layout drags it like a control. Its position is saved in `TouchOverlay.ini` (key `UT_MENU_BUTTON`, one position for every controller layout, per orientation), and Reset Overlay puts it back at the top centre.
- **A setting:** Overlay Controls → **Auto-hide Menu Button** (`[UbuntuTouch] MenuButtonAutoHide` in Dolphin.ini, on by default). Off, the button is always shown, as before.
- **Wii games:** a tap on empty screen also moves the Wii Remote's pointer, and a double tap there presses A (3.3). So a reveal tap moves the pointer too.
  - This was accepted as the simpler choice. Taps landing on the pointer anyway is mostly harmless.
  - The alternative, a reveal zone near the button, stays open if it turns out to bother.
  - Gestures from the top edge are not an option: Lomiri opens its indicator panel from there, even in fullscreen.

### How it is built (`TouchOverlay.{h,cpp}`, `OverlayLayout.{h,cpp}`)

- **Visibility:** `MenuButtonVisibility()` gives 1 while shown, fading to 0 over the last 0.4 s, and 0 when hidden (always 1 with auto-hide off). `RevealMenuButton()` sets it shown for 3 s.
- **Touch:**
  - a press on the button counts only while it is visible;
  - a press that lands on no control (and not on a visible button) reveals it, and still moves the Wii pointer as before;
  - presses on controls do not reveal it, so playing with the touch controls keeps it hidden.
- **Drawing:** the button's colours are scaled by the visibility (the fade). While a menu is open or the game is paused, it is revealed on each frame, so it is there when the menu closes.
- **Edit Layout:** the button is drawn and dragged there (`MENU_BUTTON_DRAG`), above the controls. It is saved when the finger lifts, as the controls are (`OverlayLayout::SavePosition`), and placed from the saved position in `Layout()`.
- **The overlay is rebuilt at the game's start and after layout changes** (`Build`), which reveals the button for its 3 s, so a new player sees it.

### Checked over adb

`mirscreencast` captures, Need for Speed:
1. **0.8 s after the game ran:** the Menu button at the top centre.
2. **About 9 s later, no touches:** gone.
3. **Edit Layout** (`DOLPHIN_UT_OVERLAY_MENU=edit`): drawn beside Done, among the controls' outlines.

Not checkable over adb: tapping to reveal, the fade, and dragging (they need a finger).

### Manual tests

Screen on, unlocked: `sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 1800`, then start Need for Speed from the library.

**S7-M1. Hiding.** Does the Menu button show for about 3 s at the start, then fade away? While racing with the touch controls, does it stay hidden?

**S7-M2. Revealing.** Tap an empty part of the screen:
- Does the button come back, and fade again about 3 s after?
- While it is hidden, tap exactly where it was: does that only bring it back (no menu)? Tap it again: does the menu open?
- Pause the game from the menu, close the menu: does the button stay while paused?

**S7-M3. Moving.** Menu → Overlay Controls → Edit Layout: is the Menu button there? Drag it somewhere comfortable, tap Done. Is it there in the game, and after restarting the game? Turn the phone (portrait/landscape): each orientation keeps its own place. Overlay Controls → Reset Overlay: back at the top centre?

**S7-M4. The setting.** Overlay Controls → turn **Auto-hide Menu Button** off: is the button always shown again? Turn it back on.

**S7-M5 (Wii).** In Newer Super Mario Bros.: does a tap on empty screen bring the button back without getting in the way? (It also moves the Wii pointer, as any tap there does.)

### Results

(to be filled in)

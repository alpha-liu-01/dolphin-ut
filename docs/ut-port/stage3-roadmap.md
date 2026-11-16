# Stage 3 roadmap: a touch-first QML frontend

Written 2026-11-14, after stage 2 was closed. It replaces stage 3 ("Usable on a phone") and the QML item of stage 6 in `roadmap.md`.

## Why stage 3 changed

The original stage 3 made DolphinQt's desktop Widgets UI usable on the phone (scaling, larger hit targets), with a touch-first frontend left for "later". Stages 1 and 2 showed that the desktop UI is not worth adapting:
- it works with touch but is far too small (stage 1 S1-M3);
- some windows do not fit the screen and cannot be closed (the Wii Remote mapping window, S2-M3);
- a phone needs a different flow: start, play, an in-game menu, exit.

**User decision (2026-11-14): the frontend is a reimplementation of the Android app's layout in Qt Quick (QML).** DolphinQt stays in the tree: it is upstream's, and it is useful for debugging on the phone. It is not what the click package launches.

What stages 0–2 established for the frontend:

| Finding | Consequence for the frontend |
| --- | --- |
| Qt 6.10.2 on the phone has QtQuick, QtQuick Controls (Basic, Material, Fusion, Imagine, Universal), Lomiri Components (`qml6-module-lomiri-*`) and KTactileFeedback | No QML runtime to bundle. Only the xdg-shell plugin is missing (stage 0) |
| Qt's own GL path logs `Cannot find EGLConfig` / `EGL error 0x3005` on this phone, then recovers (stage 1) | Qt Quick renders through it: force an OpenGL ES default surface format first (3.0) |
| The game picture must not live in a Wayland subsurface: Render to Main Window crashed once on fullscreen and left a stale frame after Stop on Lomiri (S2-M4) | The game gets its own top-level surface. The touch overlay and menus must not be a subsurface over it (3.0 decides how) |
| #14962's Wayland path works for a separate top-level render window, with the port's fixes (`Main.cpp`, `RenderWidget.cpp`, `VKSwapChain.cpp`, stage 2) | The frontend uses the same `WindowSystemInfo` (`wl_display` + `wl_surface`) and the same resize and surface-change calls as `RenderWidget` |
| Lomiri's environment: `QT_QPA_PLATFORM=ubuntumirclient;…`, libhybris TLS variables, `PULSE_PROP` needed for the volume keys (stages 1–2) | The launcher sets them; the frontend strips `ubuntumirclient` as DolphinQt does |
| Screensaver inhibit through `org.freedesktop.ScreenSaver` fails (stage 2) | Keep-display-on goes through Ubuntu Touch's own interface (3.2) |
| Confirm on Stop pauses the game behind a dialog nobody may see (stage 2) | No stop dialog: the in-game menu's Exit is the confirmation (3.4) |
| Vulkan ran at 55–110 % speed against about 45 % for OpenGL ES on the test game (S2-M1/M2); dual core is on by default only on Android (stage 1). In 3.0, `dolphin-ut` ran Newer SMB at a steady 60 fps on Vulkan **in single core** | Default backend Vulkan (3.8). Single vs dual core is measured from save states (S3.0-M6) before 3.8 picks one. Stage 4 confirms with more games |
| Bluetooth pads work through evdev; a DualShock 4 clone over USB is rejected by the 5.4 kernel's `hid-sony` (stages 0–1) | Controller support through SDL3 and evdev, with default mappings (3.5) |

## What the Android app has, and what the port takes

From `Source/Android/app/src/main` (Kotlin UI, with a C++ JNI layer in `Source/Android/jni` that calls Dolphin's core directly):

| Android part | Files | In the port |
| --- | --- | --- |
| Game library: a grid of covers with platform tabs, search, a details dialog, grid options, a game folder setting | `ui/main/MainActivity.kt`, `fragments/GridOptionDialogFragment.kt`, `layout/card_game.xml`, `dialog_game_details.xml`, `jni/GameList/*` | **3.1** |
| Emulation screen | `activities/EmulationActivity.kt`, `fragments/EmulationFragment.kt`, `jni/MainAndroid.cpp` (boot, surface, pause, lifecycle) | **3.2** |
| Touch overlay: buttons, D-pad, joysticks, pointer for the Wii Remote; edit layout, scale and opacity; GameCube and Wii variants | `overlay/InputOverlay*.kt`, `menu_overlay_controls_{gc,wii}.xml`, 208 button graphics in `res/drawable-*dpi`, `InputCommon/ControllerInterface/Touch/InputOverrider` | **3.3** |
| In-game menu: pause/resume, screenshot, quick save/load, save/load slots, overlay controls, settings, refresh Wii Remotes, change disc, Skylanders, Infinity Base, reset, exit | `fragments/MenuFragment.kt`, `fragment_ingame_menu.xml`, `fragments/SaveLoadStateFragment.kt` | **3.4** (Skylanders and Infinity Base deferred) |
| Controller mapping: per-control mapping, advanced mapping, profiles | `features/input/*`, `jni/Input/*` (`EmulatedController`, `ControlGroup`, `ControlReference`, `InputDetector`, `MappingCommon`) | **3.5** |
| Settings tree (Config, Graphics, …) | `features/settings/*` (`SettingsFragmentPresenter.kt` defines the menus), `jni/Config/NativeConfig.cpp` | **3.6** (a subset first) |
| User data, file picker, content import | `activities/UserDataActivity.kt`, `CustomFilePicker*` | **3.7**, Ubuntu Touch's way (content-hub) |
| Cheats, Riivolution, NetPlay, achievements, disc conversion, system update, Skylanders and Infinity figures, TV layout | `features/{cheats,riivolution,netplay,sysupdate,skylanders,infinitybase}`, `ConvertActivity.kt`, `TvMainActivity.kt` | **Deferred** (after stage 3) or not ported (TV) |

The Kotlin UI is the design reference. The C++ in `Source/Android/jni` is the closest reference for how a non-Qt frontend drives the core: boot, the surface, pause, the game list, config, input mapping. The port's own C++ layer does the same through QObjects that QML can use. The overlay graphics are Dolphin's own GPL assets and can be reused as they are.

## Rules that carry over

- Host-side work only for agents (`AGENTS.md`, roadmap "Rules for this fork"). The frontend, overlay, menus and mapping UI are host side. The settings screens must only **expose** existing settings; which defaults change emulated behavior is the user's decision (3.8).
- The new code goes in **`Source/Core/DolphinUT/`**, a new frontend next to `DolphinQt/` and `DolphinNoGUI/`, with its own CMake target (`dolphin-ut`) behind an `ENABLE_UT_FRONTEND` option. Being a new directory keeps rebases on upstream conflict-free. Build scripts and packaging stay in `ut/`.
- Changes to shared code (`UICommon`, `Core`, `InputCommon`, `VideoCommon`) stay small and separate, like the stage 2 port fixes.
- Every phase ends with on-phone tests by the user, as in stages 0–2.

## Phases

Phases 3.0–3.2 are sequential. After 3.2, phases 3.3–3.7 can go in any order; the order below is the suggested one. The sizes are rough and relative.

### 3.0 Foundations and the window decision (spike, small)

Goal: a QML app shell that starts on the phone, and a decision on how the game picture and the touch overlay share the screen.

Steps:
1. **Build setup.** Add `qt6-declarative-dev:arm64` (and its host tools: `qmlcachegen`, `qmltyperegistrar`) and `qt6-declarative-private-dev` if needed to `ut/docker/Dockerfile`. A `dolphin-ut` target in `Source/Core/DolphinUT/` that links `core`, `uicommon`, `inputcommon` and `videocommon` like DolphinQt, and Qt6 Quick and QuickControls2. `ut/build.sh` builds it and `--package` includes it.
2. **App shell.** `main.cpp`:
   - set `QT_QPA_PLATFORM` without `ubuntumirclient` (shared with DolphinQt's fix);
   - force an **OpenGL ES** default `QSurfaceFormat` before the `QGuiApplication` (the stage 1 EGL-config errors);
   - set up `UICommon` (user directory, `Init`, config load);
   - implement the `Host_*` functions the core needs (`Host_UpdateTitle`, `Host_RequestRenderWindowSize`, `Host_Message`, `Host_YieldToUI`, and so on), as `DolphinQt/Host.cpp`, `DolphinNoGUI/MainNoGUI.cpp` and `Source/Android/jni/MainAndroid.cpp` each do.
   - Check: a QML window opens on the phone with no EGL errors, takes touch, and shows the on-screen keyboard in a text field.
3. **Window spike: game picture plus overlay.** Build each candidate as a throwaway test on the phone with the test game. Measure: does it work at all, are there stale frames on Stop, can it rotate, does it cost frame rate.
   - **A. Overlay drawn by Dolphin.** The game window is a plain `QWindow` (its own top-level surface, as in stage 2's separate render window). The touch overlay is drawn into the game picture by Dolphin's renderer through its on-screen UI (ImGui, `VideoCommon/OnScreenUI`), from the same button graphics. Touches on that `QWindow` go to `InputOverrider`. Menus are separate QML top-level windows shown while the game is paused. No subsurface at all, and it works the same with Vulkan and OpenGL ES. It needs a small overlay renderer in `VideoCommon`, so check with the user whether that counts as host-side UI (it draws on top of the output and does not touch emulation).
   - **B. QML on top in a subsurface.** The game top-level window is a `QWindow`, and a transparent `QQuickWindow` child (a `wl_subsurface`) holds the overlay. It is the simplest to build, but it is the subsurface pattern that failed on Lomiri in stage 2 (stale frames, one crash), and it needs alpha blending of subsurfaces in Mir 1.8. Test only whether a fixed, never-moved subsurface is reliable.
   - **C. Game rendered into Qt Quick.** Dolphin renders offscreen and Qt Quick shows the result as a texture (shared GL context or EGLImage). That is one surface and full QML freedom, but it is the most work: a texture hand-off for both backends through libhybris, and an extra copy per frame.
   - Recommendation going in: **A**, with B tested once for comparison. The decision is the user's and is recorded in this document.
4. **Lomiri integration checks** (each is a small QML test):
   - orientation lock to landscape for the game window: the desktop file key `X-Lomiri-Supported-Orientations`, and/or `QWindow::reportContentOrientationChange`;
   - keep the display on: Ubuntu Touch's `com.canonical.Unity.Screen.keepDisplayOn` (repowerd) needs the `keep-display-on` policy group;
   - application state: `QGuiApplication::applicationStateChanged` as Lomiri suspends and resumes the app;
   - haptics through KTactileFeedback (`qml6-module-ktactilefeedback`, on the phone). **Did not work: no Qt 6 feedback plugin on the phone. Replaced by hfd-service over D-Bus (`com.lomiri.hfd.Vibrator`)**;
   - the on-screen keyboard (maliit) in QML text fields.

Exit: the shell runs on the phone, and the window architecture is decided and recorded here.

#### Status (2026-11-15): done. Option A decided by the user

Notes: `stage3.0-foundations.md`.
- Build: `ENABLE_UT_FRONTEND`, target `dolphin-ut` in `Source/Core/DolphinUT/`, Qt Quick 6.10.2 in the container.
- Shell: the QML launcher loads with the OpenGL ES default format and **no EGL-config errors** (stage 1's QML prerequisite is solved), scale 2.625 from `GRID_UNIT_PX=21`. SIGINT and SIGTERM quit cleanly (SDL's signal handlers disabled; `exit()` instead of `quit()`).
- **Spike A works without any `VideoCommon` change.** The overlay hooks `before_present_event` and draws with ImGui into the open on-screen-UI frame (0.2–0.28 ms of video-thread CPU per frame); touches go to `InputOverrider`. A Vulkan crash (several draws into one frame) was fixed by drawing once per ImGui frame.
- On-screen results (S3.0-M1…M4):
  - Passed: readable launcher; keyboard; fullscreen game with immediate touch controls; landscape kept; pause and resume on app switch; one spread entry; screen kept on; Exit; OpenGL ES works (slower); Newer SMB at a steady 60 fps on Vulkan.
  - Failed, then fixed (S3.0-M5):
    - **Wii D-pad 90° off.** The profile has desktop Dolphin's default Nunchuk attached, so the game reads the remote upright. The overlay layout now follows the configured extension, with a new Wii Remote + Nunchuk layout.
    - **Vibrate.** There is no Qt 6 haptics plugin; it now goes through hfd-service. Confirmed working.
    - The Nunchuk layout with Newer SMB works, and the D-pad direction is right.
- **Window architecture: A, decided by the user**, on condition that the Android overlay customization UX is reimplemented (3.3). The default layout looks odd in portrait; 3.3 adds portrait defaults as Android has.
- Style: the user wants the native Ubuntu Touch look (see "Visual style" below).
- Vibrate works through hfd-service (S3.0-M5).
- Speed, benchmarked from a Need for Speed: Most Wanted race save state (screen on, no input; `stage3.0-foundations.md`). The tools for it:
  - a `perf:` log line;
  - `-C` overrides;
  - overlay Save and Load (slot 1);
  - `--state-slot`;
  - `ut/phone-debug/bench.sh`.
  The results:
  - **The big cores' clock was the limit.** schedutil kept them at 1228–1516 MHz, and dual-core threads drifted onto the little cores.
  - **uclamp_min 1024 on the emulation threads** (no root needed) doubled the speed: 41 % → 79 % on Vulkan, 86 % on OpenGL ES.
  - **Dual core beats single core** at every setting.
  - OpenGL ES came out faster than Vulkan in this scene; Vulkan looked better with Newer SMB. The best backend depends on the game.
- **User decisions (2026-11-15):**
  - a CPU boost setting (heat is not an issue);
  - dual core by default;
  - per-game settings are required;
  - expose all of the Android app's features, or more where needed (for example the emulated CPU clock override, which the user sets to about 85 % for this game on Android).
- The GameCube layout lacks analog L and R (throttle and brake in Need for Speed); left for 3.3.

### 3.1 Game library (medium)

Goal: the Android main screen. Open the app, see the games, start one.

Steps:
1. A `GameListModel` (`QAbstractListModel`) over `UICommon::GameFileCache`, scanning the configured folders on a worker thread, like `DolphinQt/GameList/GameTracker.cpp`. Roles: title, platform, region, ID, cover, file path.
2. The QML grid of cover cards with platform tabs (GameCube, Wii, WiiWare; Android has no "All" tab), as `MainActivity` / `card_game.xml`.
   - Covers: Dolphin's existing GameTDB cover download (`UICommon` / `GameCovers`), cached in the user folder. Banner or title text when there is no cover.
3. Long press: the game details dialog (`dialog_game_details.xml`): title, ID, region, file, size, compression.
4. Game folders: add and remove folders. The default is the app's own data folder (3.7).
5. Grid options: sort and title display (`GridOptionDialogFragment`).
6. Refresh, and a pull-to-refresh gesture.

Exit: the test games appear with covers on the phone, and a tap opens the emulation screen (3.2).

#### Status (2026-11-15): done

Notes: `stage3.1-library.md`.
- Built, in Lomiri Components:
  - the library: worker-thread scan over `GameFileCache`, the three Android tabs, cover cards (GameTDB covers through Dolphin's own download), search, pull to refresh, rescan on return to the app;
  - the long-press properties dialog and the details page;
  - grid options;
  - game folders with a folder browser;
  - a Developer page with the 3.0 tools.
- Findings:
  - **Lomiri Components on Qt 6 do their own scaling**, so `QT_SCALE_FACTOR` scaled everything twice. It is no longer set (device pixel ratio 1, like Qt 5 Lomiri apps).
  - There is no Qt 6 Suru style for Qt Quick Controls.
  - **The bundled curl had no CA store** when cross-compiled, so every HTTPS request failed. `ut/build.sh` now sets Ubuntu's CA paths.
  - Covers are off by default outside Android (a 3.8 decision).
  - 3.0's `start()` had saved the test backend in `Dolphin.ini`; a chosen backend is now per run.
- Tools: `dolphin-ut --qml FILE` and `--grab PNG` with `ut/phone-debug/page-harness.qml` check pages from screenshots over adb.
- On-screen tests passed. The remembered tab failed (the initial `Sections` index overwrote the saved one); it is fixed and checked over adb.
- **User decision: covers on by default** (3.8).
- Found while testing: **Wind Waker hangs the GPU on Vulkan** (kernel: KGSL "GPU hang detected", 3 faults → context dropped). DolphinQt does the same, so it is not the frontend. It is not specialized shaders or the EFB/XFB options. Most likely this phone's Qualcomm Vulkan driver (V@530). Stage 4; OpenGL ES works.

### 3.2 Emulation screen and lifecycle (medium)

Goal: start, run, pause and stop a game the phone way.

Steps:
1. Boot through `UICommon` / `BootManager` with a `BootParameters` from the selected game, the window built per 3.0's decision. `WindowSystemInfo` is filled from the game window's `wl_display` / `wl_surface`, with the size (`render_surface_width/height`).
2. Resize, rotation and surface changes go through `Host::ResizeSurface`-style calls into `g_presenter`. Re-send the size once the game runs, as the stage 2 `RenderWidget` fix does.
3. Landscape lock while the game runs. Fullscreen always: no window chrome, and the panel hidden.
4. Keep the display on while running (3.0 step 4).
5. **Lifecycle:**
   - pause emulation when Lomiri deactivates or suspends the app, and stop audio;
   - resume on reactivation, with the surface recreated if Lomiri replaced it;
   - make sure the swap chain and EGL surface code survives a suspended app: the stage 2 Vulkan fixes are the starting point;
   - on Lomiri closing the app: save nothing implicitly, stop cleanly.
6. Back gesture / Escape opens the in-game menu (3.4) instead of closing.
7. The FPS and speed display as an optional setting (the OSD Dolphin already draws).

Exit: start from the library, play, switch to another app and back (the game paused and resumed), rotate, exit through the menu. No stale frames and no hangs.

#### Status (2026-11-15): done (fullscreen and statistics fixes confirmed on screen)

Notes: `stage3.2-emulation.md`.
- Steps 1, 2, 4 and the pause/resume of step 5 were already done and tested on screen in 3.0. Pausing also stops the audio: the CPU's stepping turns the sound stream off.
- **Suspension:** a game frozen mid-run with SIGSTOP for 15 s (as Lomiri does to background apps) carried on after SIGCONT, with no device loss or GPU fault.
- **Back/Escape** (step 6): a short press will open the 3.4 menu (it pauses or resumes until then), a long press stops, as on Android. Ubuntu Touch has no system Back, so this needs a keyboard; a gamepad "open menu" is for 3.4/3.5.
- **FPS and speed** (step 7): Dolphin's own statistics, a switch on the Developer page until 3.6.
- On screen (S3.2): app switching, screen lock, USB-keyboard Escape and closing from the spread work.
- **Fullscreen failed: the top panel stayed.** Lomiri needs two things:
  - `set_fullscreen` *after* the window is mapped;
  - no other window of the app showing.
  Fixed: the game window asks once exposed, and the library window is hidden first. Lomiri now answers 1080x2400 with the fullscreen state.
- **The statistics were too small.** Dolphin's UI scale came from Qt's device pixel ratio, 1 since 3.1; it is now Lomiri's grid-unit scale (2.625).

### 3.3 Touch overlay (large)

Goal: the Android overlay. Play without a controller.

Steps:
1. Overlay layouts for:
   - the GameCube controller;
   - the Wii Remote (sideways, and upright with a pointer);
   - Wii Remote + Nunchuk;
   - the Classic Controller.

   Default positions are taken from the Android app's defaults (the `InputOverlay` default layout code), scaled to 2400x1080 / 2305 px, **separately for landscape and portrait** (Android's `*_PORTRAIT_X/Y` resources). Edits are saved per orientation, as Android's `-Portrait` keys. The 3.0 spike used one layout for both, which looks odd in portrait.
2. Controls: buttons, the D-pad, analog sticks (with gate radius from `InputOverrider::GetGateRadiusAtAngle`), analog triggers (the 3.0 spike has digital L/R only; racing games need the analog ones, which the Android app presses together with the digital ones by default), and the IR pointer by touch for the Wii Remote. All feed `InputOverrider::SetControlState()`, registered per controller with `Register{GameCube,Wii}InputOverrider`, as the Android JNI does.
3. Graphics: the Android PNGs (`gcpad_*`, `wiimote_*`, `nunchuk_*`, `classic_*`, pressed and normal, `drawable-xxhdpi` for this screen's density).
4. Overlay settings, **the Android customization UX reimplemented** (the user's condition for option A): show or hide, scale, opacity, show or hide individual controls, edit mode (drag to move; on the game window's touch path, moving a control instead of pressing it), reset to defaults, haptic feedback on press (hfd-service `vibrate(ms)`, as in 3.0; KTactileFeedback has no Qt 6 backend on the phone).
5. The layout follows the controller's configured extension and the "Sideways Wii Remote" option, as the Android app does (3.0 found the D-pad off by 90° with the wrong assumption).
6. Which controller the overlay drives (port 1 by default) and whether it hides when a gamepad is in use.
7. Multi-touch: several controls at once, including stick and buttons together; check that `fts_ts` reports enough points (10 in stage 1's Qt log).

Exit: the GameCube test game and a Wii game are playable with touch only, with the overlay edited and saved.

#### Status (2026-11-15): done (3.3a and 3.3b passed on screen; a latched L/R now also holds the analog trigger, unlike the Android app, re-tested)

- 3.3b: the Android "Overlay Controls" menu and edit mode, drawn into the game picture with ImGui:
  - Edit Layout (drag, saved per layout and orientation in `Config/TouchOverlay.ini`);
  - Toggle Controls and Latching Controls;
  - Adjust Controls (scale and opacity);
  - Relative Stick Center;
  - Choose Controller;
  - Touch IR Pointer (recenter, mode, double tap button);
  - Reset Overlay;
  - plus Show Overlay.

  It opens from a "Controls" button at the top centre until the 3.4 menu exists. Settings are saved in Android's `Dolphin.ini` keys, and the overlay is rebuilt, with old textures retired safely. The pages were checked from screen captures; touch interaction is for the on-screen tests.

Notes: `stage3.3-overlay.md`.
- Built:
  - the five Android layouts (GameCube, Wii Remote upright with the pointer, sideways, + Nunchuk, Classic), each in landscape and portrait;
  - the Android graphics;
  - Android's sizes and default positions, generated from the Android sources by `ut/tools/gen-overlay-defaults.py`;
  - Android's touch behavior, including digital L/R also pressing the analog trigger;
  - settings under the Android app's own `Dolphin.ini` keys.
- Portrait gives the game the top half, as Android does. This needed a small host-side addition to the presenter (`Presenter::SetDrawArea`).
- The images are ImGui user textures, freed with the on-screen UI.
- All layouts were checked from `mirscreencast` captures over adb.
- Haptics: the Android overlay has no haptic feedback on presses, so step 4's "haptic feedback on press" is not an Android feature; dropped unless the user wants it. The Android app does pass the game's **rumble** to the phone's vibrator (`DolphinVibratorManager`, "Device Sensors: Motor"); that belongs to 3.5, through hfd-service like the 3.0 Vibrate test.

### 3.4 In-game menu (medium)

Goal: Android's `MenuFragment`.

Steps:
1. A side panel or sheet over the paused game, opened by back / Escape / a two-finger tap / an overlay button. With option A, it cannot be a subsurface over the game. **User decision (2026-11-15): draw it into the game picture, like the overlay, and see whether that works well.** The overlay settings dialogs (3.3) are drawn the same way. A separate top-level QML window stays the fallback.
2. Entries: pause/resume, screenshot, quick save, quick load, save state slots and load state slots (with timestamps, as `SaveLoadStateFragment`), overlay controls (the 3.3 settings), settings (3.6), refresh Wii Remotes, change disc, reset, **exit** (stops and returns to the library; no separate Confirm on Stop dialog).
3. Not now: Skylanders and Infinity Base portals (deferred with the figures UI).

Exit: every entry works on the phone with the test games.

#### Status (2026-11-15): on-screen tests done; two fixes to re-test (closing the menu: "Close" and a tap beside the panel; no "State not found" per empty slot)

Notes: `stage3.4-menu.md`.
- The Android in-game menu is a left panel drawn into the picture with the Android entries and labels:
  - Pause/Unpause;
  - Screenshot;
  - Quick Save/Load (slot 9);
  - Save/Load State, six slots with times (Android's slots 0–5, so the files match);
  - Overlay Controls (3.3);
  - Refresh Wii Remotes;
  - Change Disc (from the game folders);
  - Reset;
  - Exit.
- Settings come with 3.6.
- It opens from a "Menu" button at the top centre or with Back/Escape.
- Save states appear only with `Core.EnableSaveStates`, which is off by default as on Android; a Developer switch covers it until 3.6. The default is the user's call (3.8).
- **A paused game presents nothing, so the menu froze.** While paused with a menu open, a redraw is queued about 30 times a second: a job on the paused CPU thread pushes an `AsyncRequests` event, which triggers the overlay hook and `Presenter::Present()`. No core change; checked with `--pause-after`.

### 3.5 Controllers and mapping (medium)

Goal: plug in or pair a gamepad and play, without a desktop mapping window.

Steps:
1. Input backends: SDL3 (built in) and evdev. Hotplug while the app runs.
2. **Default mappings:** when a known gamepad appears and port 1 has no mapping, map it to the GameCube controller and to a Wii Remote + Nunchuk / Classic Controller. SDL3's game controller database gives a standard layout for most pads, including the BETOP T6 in Xbox mode (`045e:02e0`, stage 0). Keep evdev as the fallback.
3. **Mapping UI** like Android's: a list of the controller's controls; tap one, then press the gamepad button (`InputDetector`, `MappingCommon`). Simple and advanced (expression) modes. Profiles: save, load, delete (the `Profiles/` folders).
4. Which device drives which port. Gamepad rumble on and off.
5. Optional, for USB: try SDL3's HIDAPI PS4 driver over libusb for the DualShock 4 clone the kernel rejects (stage 1). If it does not work, it stays a stone-ut kernel item.

Exit: a Bluetooth gamepad plays the test games with default mappings, and a remap through the new UI works.

### 3.6 Settings (large, in two passes)

Goal: the settings that matter on a phone, in the Android layout. The aim is everything the Android app exposes, or more where needed (user, 3.0). Until the second pass covers it, the rest stays reachable through the INI files or DolphinQt.

Steps:
1. A generic settings model over Dolphin's `Config::Info` (bool, int, float, string, choice): reads layers, writes the base layer, applies per-game overrides like Android's `NativeConfig`.
2. **First pass:**
   - Config: General (dual core, speed limit, **CPU boost** (the port's own: uclamp_min on the emulation threads while a game runs, from the 3.0 benchmark), the emulated CPU clock override (`Core.Overclock`, exposed as on Android; default unchanged), ...), Interface (OSD and FPS display, overlay defaults), Audio (backend, volume, stretching), Paths, GameCube (language, memory cards), Wii (language, aspect ratio, sensor bar position, widescreen);
   - Graphics: backend (Vulkan, OpenGL ES), internal resolution, aspect ratio, vsync, shader compilation mode, async shaders, anisotropic filtering, a few enhancements;
   - per-game settings for the same subset (long press in the library → Game settings). **Required** (user, 3.0): the best backend differs by game (Newer SMB: Vulkan; Need for Speed: OpenGL ES), and the CPU clock override is per game. The CPU boost is per game too.
3. Second pass (after stage 4 shows what is useful on this phone): more Graphics enhancements and hacks, advanced options, log settings.
4. The menu structure follows Android's `SettingsFragmentPresenter.kt`, so users of the Android app find things in the same place.

Exit: the first-pass settings change behavior on the phone and persist.

### 3.7 Files and storage (small to medium)

Goal: games get onto the phone and saves stay put, within click confinement.

Steps:
1. User folder: the app's confined data folder (`~/.local/share/<app id>/`), not `~/.local/share/dolphin-emu`.
2. Game import: **content-hub** (Lomiri Content, `qml6-module-lomiri-content` is on the phone) to import from the File Manager or downloads, copying into the app's game folder. Also a fixed folder that MTP can reach (check which paths a confined app may read).
3. MicroSD: whether a confined app may read removable media, or the user must import.
4. Export: save states, screenshots and memory cards back through content-hub (optional).

- **Screenshots to `~/Pictures/Dolphin`** so the Gallery shows them (found in 3.4: Dolphin saves them in its user folder). Needs the `picture_files` policy group in the click.

Exit: a game copied from a PC over MTP, or shared from the File Manager, shows up in the library and runs, all inside confinement (with stage 5's AppArmor profile, or `unconfined` while developing).

### 3.8 Defaults for this phone (small)

Goal: a first launch that plays well without visiting the settings.

Shipped default `Dolphin.ini` / `GFX.ini` (base layer; the user can change all of it):
- `Core.EnableSaveStates`: off as in the Android app, or on (3.4: the in-game menu's save state entries need it). The user's call;
- `CPUThread = True` (dual core, as on Android). **Decided by the user (3.0)**; in the 3.0 benchmark dual core beat single core at every setting (79 % against 41 % with the CPU boost);
- the CPU boost on (heat is not an issue, user 3.0), at the uclamp_min value stage 4 settles on (1024 in the 3.0 benchmark);
- Wii Remote 1's extension in the default profile: none (Android; the sideways overlay) or Nunchuk (desktop default). A controller default, the user's call;
- graphics backend Vulkan (faster than OpenGL ES in stage 2 and with Newer SMB in 3.0; slower in the 3.0 Need for Speed benchmark), with OpenGL ES as the fallback when Vulkan fails to start, and per-game overrides (3.6) for games that run better on OpenGL ES. The global default is checked against stage 4's game list;
- the OSD FPS display off;
- `General.UseGameCovers = True`: game covers from GameTDB, as in the Android app. **Decided by the user (3.1)**; upstream turns it on only on Android;
- no Confirm on Stop (the menu's Exit replaces it);
- `RenderToMain` is not needed (the frontend has no main window to render into).

Launcher environment (shared with stage 5): `PULSE_PROP="media.role=multimedia"`, `QT_PLUGIN_PATH` for the bundled xdg-shell plugin, `QT_WAYLAND_DISABLE_WINDOWDECORATION=1`. The TLS variables come from the session.

**Settings that change emulated behavior** (for example speed hacks, CPU clock override, accuracy options) are not changed by the port. Their defaults stay upstream's unless the user decides otherwise.

Exit: a fresh install on the phone starts the test game with these defaults.

## Removed from the old stage 3

| Old step | Why removed |
| --- | --- |
| 4. UI scaling of the Qt Widgets UI (`QT_SCALE_FACTOR`, bigger hit targets) | The Widgets UI is not shipped |
| `RenderToMain = False` as a default (stage 2) | No desktop main window in the frontend. It stays relevant only for DolphinQt used for debugging |
| Confirm on Stop behavior | Replaced by the in-game menu's Exit (3.4) |
| Overlay "drawn by the frontend" in Qt Widgets | Replaced by 3.3 per the 3.0 decision |

Kept and folded in: controllers (3.5), touch controls (3.3), window and lifecycle (3.2), files (3.7), defaults (3.8).

## Deferred to after stage 3

- Cheats (Gecko / Action Replay), Riivolution, Skylanders and Infinity Base, NetPlay, RetroAchievements, GBA link, disc conversion, Wii system update, the user data import/export screen. All exist in the Android app and can follow the same pattern later.
- A TV / big-screen layout.
- Translations of the new QML strings (Dolphin's gettext catalogs cover the core and DolphinQt only).

## Exit for stage 3

On the phone, from a fresh install (unconfined is fine until stage 5):
1. open the app, see the games with covers;
2. start a GameCube game and a Wii game;
3. play each with the touch overlay, and with a Bluetooth gamepad using default mappings;
4. switch to another app and back (paused and resumed), and rotate;
5. save and load a state from the in-game menu;
6. exit to the library;
7. change a graphics setting and see it applied on the next start.

No stale frames, no hangs, no crashes along the way.

## Decisions for the user

| Decision | Options | Recommendation |
| --- | --- | --- |
| Window architecture (3.0) | A: overlay drawn by Dolphin's renderer; B: QML overlay in a subsurface; C: game rendered into Qt Quick | **Decided (user, 2026-11-15): A**, on condition that the Android overlay customization UX is reimplemented (3.3) |
| Visual style | QtQuick Controls Material (closest to the Android app), or Lomiri Components / Suru (native Ubuntu Touch look) | **User preference (S3.0): the native Ubuntu Touch look.** Lomiri Components (`qml6-module-lomiri-components`, on the phone) for the app screens from 3.1 on; the Android app's layout is kept, only the styling changes. The in-game overlay is drawn by Dolphin (option A), so the style does not apply to it |
| CPU boost while a game runs (3.0 benchmark) | none (schedutil alone: about 41 %); uclamp_min on the emulation threads at 512 (65 %) or 1024 (79 %), dropped at pause and stop | **Decided (user, 2026-11-15): a setting (3.6), on by default (3.8), per game too.** Heat is not an issue on this phone |
| Dual core default (3.8) | upstream's Linux default (single core) or dual core as on Android | **Decided (user, 2026-11-15): dual core** |
| Per-game settings (3.6) | | **Required (user, 2026-11-15)**: the best backend differs by game |
| DolphinQt in the click package | ship it as an "advanced" second app, or leave it out | leave it out; keep it for debugging over adb |
| Where the code lives | `Source/Core/DolphinUT/` (a new frontend) or `ut/frontend/` | `Source/Core/DolphinUT/` |

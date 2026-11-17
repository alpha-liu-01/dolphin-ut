# Stage 7: release follow-ups (an extension of stage 5)

Added 2026-11-17 (the user): work around the first OpenStore release, some of it after it. The overview is in `roadmap.md`, stage 7.

| Item | When | Status |
| --- | --- | --- |
| The in-game menu button: hide during play, movable | before the release | done (S7-M1–M5 passed) |
| Development aids out of the release build | before the release | done over adb (below); one look on screen pending |
| Build documents and the Readme | before the release | written (below) |
| Ubuntu Touch 24.04-1.x (the Nothing Phone (1) would not install the click) | before the release | a second click, built and checked over adb (below); on-screen tests pending |
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

2026-11-17: **all passed** (S7-M1–M5: hiding, revealing, moving and its reset, the setting, a Wii game).

## Development aids out of the release build

The user's question: does the release build still include redundant, or even harmful, debug features? It did. The click packaged the same `dolphin-ut` binary as the development builds (`build-ut/dolphin`), with every aid added for tests over adb since stage 3.

### What was in it, and what happens to it

| Aid | Since | Risk in a release | Release build |
| --- | --- | --- | --- |
| `--qml FILE`: loads any QML file as the whole UI, with the app's objects | 3.1 | **Harmful:** runs arbitrary code if anything ever passes arguments to the app | removed |
| `--start PATH` with `--backend`, `--riivolution`, `--stop-after`; `--state-slot N`; `--pause-after MS` | 3.0–6.4 | redundant (test runs) | removed |
| `--settings-after`, `--return-after`, `--test-alert`, `--answer-after`, `--test-action`, `--test-restart-after` | 6.1–6.8 | redundant; they raise fake alerts, answer dialogs, run settings actions, restart the app | removed |
| `--grab PNG` (`--grab-after`): saves a screenshot of the app's window | 3.1 | redundant | removed |
| `DOLPHIN_UT_GPU_DRIVER`: forces a GPU driver folder | 4 | **can override the user's driver choice** without a sign of it | removed |
| `DOLPHIN_UT_OVERLAY_MENU`: opens a menu page at the game's start | 3.3 | redundant | removed |
| `DOLPHIN_UT_SENSOR_LOG`: logs the motion sensors once a second | 6.6 | redundant | removed |
| A "perf: … fps …" line every 5 s while a game runs | 3.0 | log spam (the journal, the App Log) | removed. The same timer re-applies the CPU boost to new threads; that stays |
| "overlay: … us per frame" every 600 frames | 3.3 | log spam | removed |
| Developer page: "Hide this window while playing", "Pause when the app is not active" | 3.0 | **Harmful:** switched off, they break fullscreen (3.2, 5) and the pause in the background (Lomiri stops a backgrounded app) | removed (development builds only) |
| Developer page: "Vibrate", a keyboard test field | 3.0 | redundant | removed |
| Developer page: the live log | 3.0 | useful for bug reports | **kept**, as **App Log** |

Kept in both: Dolphin's own `-u/--user` (user folder) and `-C System.Section.Key=Value` (a config override for that run), as DolphinQt and dolphin-emu-nogui have them; and the debug settings of Config → Debug, which the Android app has too.

### How

- **The switch:** a CMake option, `DOLPHIN_UT_DEV_TOOLS` (`Source/Core/DolphinUT/CMakeLists.txt`), on by default, so the development builds and every test script stay as they were. Off, the aids above are not compiled (`#ifdef DOLPHIN_UT_DEV_TOOLS` in `main.cpp`, `GpuDrivers.cpp`, `PhoneSensors.cpp`, `TouchOverlay.cpp`, `Emulator.cpp`).
- **QML** gets `devTools` (true or false) for the App Log page's development rows.
- **The App Log page** (`DeveloperPage.qml`, library ⋮ → **App Log**, was "Developer"): the log, with a note on writing Dolphin's own log to a file and sending it (Config → Log; User Data → Send a File to Another App). The messages that pointed to "the log on the Developer page" now say "the App Log".
- **The release build:** `ut/build.sh --release` builds `build-ut/release` with `-DDOLPHIN_UT_DEV_TOOLS=OFF`, and only `dolphin-ut` (DolphinQt, dolphin-emu-nogui and dolphin-tool off: the click never shipped them).
- **The click:** `ut/click/build-click.sh` packages that build, not `build-ut/dolphin`. The release command is now `ut/build.sh --release && ut/click/build-click.sh`.

### Checked (2026-11-17)

| Check | Result |
| --- | --- |
| Development build (`build-ut/dolphin`, aids on) | builds as before; the test scripts are unchanged |
| Release build (`ut/build.sh --release`) | 1804 steps, `dolphin-ut` only, no new warnings (stage 4's sign-compare in `GpuDrivers.cpp` and Qt's generated files, as in the development build) |
| The aids gone from the binary | strings searched in both binaries: `--qml`, `--test-alert`, `--grab`, `DOLPHIN_UT_GPU_DRIVER`, `DOLPHIN_UT_OVERLAY_MENU`, `DOLPHIN_UT_SENSOR_LOG`, the test alerts' texts, "perf: %1 fps", "us per frame", "phone sensors: accel" (the last three as UTF-16, as `QStringLiteral` stores them): all in the development binary, **none in the release one** |
| The click from it | `dolphin-ut.alphaliu01_2609.276_arm64.click`, 14.0 MB; installed on the phone over the earlier 2609.266 (user data kept) |
| Starting it through Lomiri | normal start in the journal (user folder, game list, hotkeys' first start, input devices); closed cleanly. `lomiri-app-launch` itself printed "Lost our connection with the registry" after "Started", a crash of that command-line tool (seen with the screen locked), not of Dolphin |

**S7-M6 (a look).** In the installed Dolphin (from the app drawer, not "Dolphin (dev)"): library ⋮ → **App Log**. Is there only the log, with its note, and no switches, Vibrate button or keyboard field?

## Build documents and the Readme (2026-11-17)

The user's request: complete the build documents and the Readme before the release.
- **[UbuntuTouchSetup.md](../../UbuntuTouchSetup.md)** (new, at the top of the tree next to Android's `AndroidSetup.md`), for someone building the port:
  - the target and the one tested phone;
  - prerequisites (an x86_64 Linux computer with Docker; developer mode and adb for the phone);
  - the cross container;
  - the release and development builds;
  - packaging the click and its settings (`CLICK_NAME`, `CLICK_MAINTAINER`, `CLICK_VERSION`, `CLICK_CONFINED`), and the GPL source link;
  - installing;
  - the development loop on a phone (the scripts, the Qt 6 shell plugin for development runs, the GPU driver shim, the development aids, screen captures);
  - the generated files and when to regenerate them;
  - where the port's code is, troubleshooting, and the contribution rules.
- **[Readme.md](../../Readme.md)**, with Dolphin's text unchanged and three additions:
  - a note at the top that this fork carries an unofficial Ubuntu Touch port, not supported by the Dolphin team, with links;
  - **Ubuntu Touch** under System Requirements (24.04-2.x arm64; the phone's GPU driver through libhybris; the tested device);
  - **Ubuntu Touch** in the Building list, and a short "Ubuntu Touch-specific instructions" section with the three commands.
- **[docs/ut-port/README.md](README.md)** (new): an index of these notes, one line per document.
- `stage1-build.md` and `stage5-packaging.md` now open with a pointer to `UbuntuTouchSetup.md` for the current instructions.
- Sizes in the guide were measured: the container image 2.3 GB, the source with submodules about 7 GB (with git's history), each build tree a few hundred MB.


## Ubuntu Touch 24.04-1.x: a second click (2026-11-17)

### The report

The user's report: a Nothing Phone (1) on Ubuntu Touch 24.04-1.x "somehow failed to install dolphin-ut". Reproduced over adb (serial `afd1c537`, device `Spacewar`, Snapdragon 778G+ (`lahaina`), Adreno 642L, Android 11 vendor, 24.04-1.x daily build 496):
- `com.lomiri.click.Install` (and OpenStore, from the File Manager): "failed to install".
- `click verify`: **`Framework "ubuntu-touch-24.04-2.x" not present on system`**. 1.x has `ubuntu-touch-24.04-1.x` (and its `-qml`/`-papi` variants), not 2.x.

### Why the framework name alone is not the fix

| | 24.04-2.x (the stone) | 24.04-1.x (the Nothing Phone) |
| --- | --- | --- |
| Qt | 6.10.2 (and 5.15) | **5.15 only: no Qt 6 package or library at all** |
| Lomiri Components, Lomiri Content (QML) | Qt 6 builds | Qt 5 builds only |
| maliit input context | Qt 6 | Qt 5 |
| Vulkan | `libvulkan1-hybris` (libhybris' own loader) | the Khronos loader with no driver: no `libvulkan1-hybris`, no `vulkanplatform_*.so` in libhybris |
| AppArmor policy versions | up to 2404.2 | up to 2404.1 |

`dolphin-ut` is a Qt 6 app (Qt Quick, Lomiri Components for Qt 6, Qt 6's private Wayland headers). The user asked not to port it back to Qt 5. So the 1.x click brings its Qt 6 along; the system cannot get Qt 6 otherwise (read-only root filesystem).

### What the 1.x click bundles

From UBports' 24.04-2.x repository, the same packages the app is built against, unpacked by the cross container into `/opt/ut-runtime-1x` (list: `ut/docker/runtime-24.04-1.x.list`):
- **Qt 6.10.2:** Core, Gui, DBus, Network, OpenGL, Qml (+Meta, Models, WorkerScript), Quick (+Layouts), Svg, ShaderTools, WaylandClient, WlShellIntegration; plugins `platforms/libqwayland.so`, the wayland-egl client integration, xdg-shell, image formats, the SVG icon engine, TLS. Left out: the EGLFS, xcb and Wayland compositor parts.
- **QML modules:** QtQuick, QtQml, QtCore, Qt.labs.folderlistmodel, Qt5Compat.GraphicalEffects, Lomiri Components (with Pickers, Popups, Labs, the themes), Lomiri Content, Lomiri PerformanceMetrics, KDE tactile feedback. Lomiri Components and Content need the Qt 6 builds of liblomiritoolkit/gestures/metrics/systeminfo, liblomiri-content-hub and the download manager client.
- **The keyboard:** maliit's Qt 6 input context (`QT_IM_MODULE=maliit` is set by the session).
- **Two base libraries 1.x lacks:** `libb2` (Qt Core) and `libOpenGL` (`libopengl0`, Qt Gui).
- **Vulkan through libhybris:** 2.x's `libvulkan.so.1` (`libvulkan1-hybris`), its Wayland platform plugin `vulkanplatform_wayland.so` and `libhybris-vulkanplatformcommon`. The library needs only `android_dlopen`/`android_dlsym` from libhybris-common, and the plugin only what 1.x's libhybris-platformcommon, gralloc, libsync and libwayland export (checked symbol by symbol against the phone's libraries).
- **Licences:** each bundled package's `copyright` file, in `licenses/`.

What 1.x already has was checked the same way: every library the bundled files need (ICU, OpenSSL, libwayland, libxkbcommon, glib, boost_log, libhybris' EGL, …) is on the phone, by soname from its `ldconfig -p`.

The click is 33.7 MB (the 2.x click: 14.0 MB).

### The phone's own Vulkan driver on 1.x

The first run on Vulkan fell back to OpenGL ES: `library "vulkan.adreno.so" not found`. Android's Vulkan loader asks libhybris' "sphal" namespace for the driver, and 1.x's libhybris does not search `/vendor/lib64/hw` there (2.x's, with its Vulkan support, does).

The stand-in `libvndksupport.so` (stage 4, `ut/vkdriver`) already loads a custom driver from a folder in a namespace of its own. It now also takes **`DOLPHIN_UT_VK_SYSTEM_DIR`**: when set, a `vulkan.*` library Android's loader asks for comes from `<dir>/<that name>` (so any vendor's `vulkan.<hardware>.so`, not only Adreno). A custom driver chosen in the app still comes first; anything not found goes the usual way, which ends in the OpenGL ES fallback.

### How the pieces are wired

- **`ut/docker/Dockerfile`:** a layer downloads the list and unpacks it into `/opt/ut-runtime-1x`, and the libhybris package into `/opt/ut-hybris-1x` (only its Vulkan plugin is used).
- **`ut/click/build-click.sh`:** `CLICK_UT=24.04-1.x` sets the framework `ubuntu-touch-24.04-1.x`, AppArmor `policy_version` 2404.1, copies the runtime into the click's `lib/aarch64-linux-gnu/` (libraries, `qt6/plugins`, `qt6/qml`, `libhybris/`), and writes the click to `build-ut/ut-24.04-1.x/`. The default (`24.04-2.x`) is unchanged; `manifest.json.in` now takes the framework as `@FRAMEWORK@`.
- **`ut/click/launcher.sh`:** when the click has `qt6/qml` (the 1.x click only), it sets `QML_IMPORT_PATH`/`QML2_IMPORT_PATH` to it; when it has `libhybris/`, it sets `HYBRIS_VULKANPLATFORM_DIR`, puts the shim first in `HYBRIS_LD_LIBRARY_PATH` (before 1.x's default `/system/lib64:/odm/lib64:/vendor/lib64`), and sets `DOLPHIN_UT_VK_SYSTEM_DIR=/vendor/lib64/hw`. The 2.x click has neither folder, so nothing changes there.
- **`ut/click/install-click.sh`:** without a file, it reads the phone's frameworks and installs the newest click built for its release (`build-ut/` or `build-ut/ut-24.04-1.x/`). `ANDROID_SERIAL` picks the phone when several are connected.
- **Development on a 1.x phone:** Lomiri 1.x does not answer a Wayland client started from an adb shell (`wl_display.get_registry` gets no reply, Qt reports "Loading shell integration failed"), so `run-dolphin.sh` cannot show a window there. `launch-dolphin.sh` (the "Dolphin (dev)" entry, `dev-install.sh`) now uses the installed 1.x click's runtime when there is no `qt6w` folder, and tests go through it with `dolphin-ut-launch.args`. Screen captures need `mir_socket_trusted` there.

### A deadlock found on the way: a lost GPU during an EFB access

Wind Waker loses the GPU on the vendor Vulkan driver (`VK_ERROR_DEVICE_LOST`, stage 4 saw the same on the stone's Adreno 619). The app is meant to stop the game and start it again on OpenGL ES (stage 3.8). On the Nothing Phone it hung instead: the game picture froze and the app could not be closed.

A core dump (with `core-info.py`, which now takes a thread ID or `all`) showed the cycle:
1. The GPU thread's command buffer submit failed. `NoteAlert` queued a stop to the UI thread, then the alert went on to `AskFromAnyThread`, queued behind the stop, with a blocking call.
2. The UI thread ran the stop: `Core::Stop` waits in `CPUManager::Stop` for the CPU thread.
3. The CPU thread was in an EFB depth peek (`HardwareEFBInterface::PeekDepthInternal`), waiting for the GPU thread, which waited for the UI thread.

Whether it hangs depends on what the CPU thread is doing at that moment, so 2.x could hit it as well. Two changes (`Emulator.cpp`, frontend only):
- **A lost device is not asked about:** `NoteAlert` logs it ("alert: … — stopping the game"), queues the stop and answers it at once. The restart on OpenGL ES already tells the user on screen.
- **No question during a stop:** `AskFromAnyThread` answers "continue" without asking when the core is `Stopping` (the UI thread is waiting for the emulation threads and cannot answer them), and logs "alert during a stop: not asked, continuing".

### Checked over adb (Nothing Phone (1), 24.04-1.x)

| Check | Result |
| --- | --- |
| Install of the 2.x click | fails: framework not present (as reported) |
| Install of the 1.x click (`install-click.sh`, picks it by the phone's framework) | installed, `dolphin-ut.alphaliu01 2609.278` (now `2609.278.1x`, below); AppArmor policy 2404.1 |
| Start from Lomiri (`lomiri-app-launch`) | Qt platform `wayland`, Lomiri Components pages, the library window; capture: the library drawn normally (Lomiri theme, fonts, icons). Wind Waker listed from `Documents/Dolphin/Games` |
| Wind Waker on OpenGL ES (first run, before the driver fix: the fallback) | runs, `JITARM64 DC \| OpenGL ES` |
| Wind Waker on Vulkan (driver through the shim) | `[vkdriver-shim] /vendor/lib64/hw/vulkan.adreno.so … loaded`, `JITARM64 DC \| Vulkan`, 50 fps (PAL), 100 % speed |
| The lost GPU (before the fix) | after 12 s: `VK_ERROR_DEVICE_LOST`, then the deadlock above |
| The lost GPU (after the fix) | after 12 s: "alert: … VK_ERROR_DEVICE_LOST (-4) — stopping the game", stopped in 0.13 s, restarted on OpenGL ES, running |
| The 2.x click from the same tree | unchanged: framework 2.x, policy 2404.2, 14.0 MB, no bundled Qt (not re-installed: the stone was not connected) |

Seen once, not explained: one Vulkan start waited 73 s between "start" and the game window being created (the window's creation waits on Lomiri). The other starts took under 0.2 s. Possibly the display was off; S7-M8 checks it with a finger.

### Versions for two clicks (2026-11-17)

OpenStore takes several clicks of one app if their frameworks differ, offers each phone "the latest version which is still compatible", and needs every click's version to be unique (UBports' guide to publishing for 24.04-1.x). Two facts decide the suffix:
- **2.x phones accept 1.x clicks:** 2.x's `ubuntu-sdk-libs` installs the `ubuntu-touch-24.04-1.x*` frameworks next to its own. So on a 2.x phone OpenStore offers the higher of the two versions, and the 1.x click's must sort lower.
- **No `~`:** the user asked for `~1x` (`2609.278~1x` < `2609.278`). Built and installed, the app started, but Lomiri logged `Invalid app ID` and `long App ID encountered`. systemd escapes `~` in the app's unit name (`…_2609.278\x7e1x--.service`), and lomiri-app-launch cannot read the app ID back from it. The app ran unmanaged: with the File Manager in front, Lomiri did not pause it (`S`), while the normal-version instance next to it was paused (`T`). In Debian version order only `~` sorts below the bare version, so the 2.x click needs a suffix too.

**The scheme:** `.2x` for 24.04-2.x and `.1x` for 24.04-1.x (`build-click.sh`, after a `CLICK_VERSION` too): `2609.278.1x` < `2609.278.2x`, both above the earlier `2609.276` and `2609.278` (the stone's and the Nothing Phone's installs upgrade). Checked on the Nothing Phone: `2609.278.1x` installed over `2609.278~1x`, no app ID warnings, and Lomiri paused it behind the File Manager (`T`).

### OpenStore review (what was checked, 2026-11-17)

Manual review is needed with or without gamepads:
- **Gamepads need the unconfined template.** Every confined template on the phone (`default`/`ubuntu-sdk`, `ubuntu-webapp`, …, policy 2404.1) has `audit deny /dev/input/** rw` and `deny /run/udev/data/** r`, and none of the 29 policy groups grants input devices (UBports' AppArmor docs list the same 29). OpenStore reviews unconfined apps by hand.
- **A confined build would be reviewed by hand too:** `document_files` and `picture_files` (games in Documents, screenshots in Pictures) are "Usage: reserved" groups, and "reserved usage policies will need to be manually reviewed" (UBports' AppArmor docs). The other groups (audio, keep-display-on, networking, content_exchange, content_exchange_source, sensors) are common.
- **Bundled Qt 6:** OpenStore's automatic review refused a click with Qt 6 libraries ("Unsupported `6.8` version of Qt used") until a store maintainer added an exception (UBports forum, "A Qml6 webapp: Touch piano"). Both clicks carry Qt 6 files (the 1.x one all of Qt 6; the 2.x one the xdg-shell plugin and its library), so ask for that exception with the submission.

### Manual tests (Nothing Phone (1), screen on, unlocked)

Wind Waker is in `Documents/Dolphin/Games`.

**S7-M7. The app.** Open **Dolphin** from the app drawer. Does the library look right (tabs, Wind Waker's cover once downloaded, the ⋮ menu)? Open Settings and a few pages; tap a text field (the search): does the on-screen keyboard come up and type?

**S7-M8. A game.** Tap Wind Waker. Does it start within a few seconds, with picture and sound? After it loses the GPU on Vulkan (about 10 s in, as on the Redmi): does it restart by itself on OpenGL ES, with the yellow note, and play on? Then, in its game settings (long press → Edit Game Settings), choose OpenGL ES as the video backend and start it again: does it play without the restart?

**S7-M9. Leaving.** In the game: Menu → Exit Emulation. Back at the library? Close the app from the app switcher (swipe up): does it close, and open again normally?

# Stage 3.0: foundations and the window spike

Phase 3.0 of `stage3-roadmap.md`. Goals:
- a QML app shell that starts on the phone;
- a decision on how the game picture and the touch overlay share the screen;
- checks of the Lomiri integration points.

Started 2026-11-14, on branch `ut/wayland` (stage 2 fixes committed), same phone and build container as before.

## 1. Build setup

- `ut/docker/Dockerfile`: added `qt6-declarative-dev qt6-declarative-dev-tools` (amd64: `qmlcachegen`, `qmltyperegistrar`, `qmlimportscanner`) and `qt6-declarative-dev:arm64`, all UBports 6.10.2 like the phone. `qt6-declarative-dev-tools` is `Multi-Arch: foreign`, so the amd64 tools serve the arm64 build. The QML runtime modules are already on the phone, so nothing extra is shipped.
- Top-level `CMakeLists.txt`: a new option `ENABLE_UT_FRONTEND` (default off). `Source/Core/CMakeLists.txt` adds `DolphinUT` when it is on. These are the only changes to shared build files.
- `ut/build.sh` turns it on and packages `dolphin-ut` next to the other binaries.
- `Source/Core/DolphinUT/CMakeLists.txt`: target `dolphin-ut`.
  - Links `core`, `uicommon`, `inputcommon`, `videocommon`, `imgui`, Qt6 Quick, QuickControls2, DBus and GuiPrivate. It needs `qpa/` headers for the native `wl_display` / `wl_surface`, as DolphinQt does.
  - The QML module `DolphinUT` uses Qt policies QTP0001 and QTP0004 (NEW). Without QTP0001 the module landed outside `qrc:/qt/qml/`, and `loadFromModule("DolphinUT", "Main")` failed with "Module DolphinUT contains no type named Main".
  - `InputCommon/ControllerInterface/Touch/InputOverrider.cpp` is compiled into this target: `InputCommon` builds it only for Android, and the file has no Android dependency.
- Build: about 40 extra steps on top of the stage 2 build. `dolphin-ut` is 28 MB; the package is now 51 MB.

## 2. App shell (`Source/Core/DolphinUT/`)

| File | What |
| --- | --- |
| `main.cpp` | Environment setup (below), `QGuiApplication`, the Material style, `UICommon` init, the QML engine; `--user`, `--start PATH --backend NAME` (for adb tests); SIGINT/SIGTERM handling |
| `Host.cpp` | The 19 `Host_*` functions from `Core/Host.h` (mostly no-ops, as in DolphinNoGUI). `Host_Message(WMUserStop)` stops the game; `Host_RendererHasFocus` follows the game window |
| `Emulator.{h,cpp}` | The QObject QML uses: the game list (spike: the files in the configured game folders), `start(path, backend)`, `stop()`, the core state, a log (also printed to stdout as `[dolphin-ut] …`), app-state handling, the game window and overlay |
| `GameWindow.{h,cpp}` | The game's own top-level `QWindow`. Qt never draws into it; the video backend presents to its `wl_surface`. Touches go to the overlay; a close request stops the game |
| `TouchOverlay.{h,cpp}` | Spike option A (section 3) |
| `LomiriIntegration.{h,cpp}` | `keepDisplayOn` / `removeDisplayOnRequest` over D-Bus; orientation hint and logging |
| `qml/Main.qml` | A test launcher: game list with "Vulkan" / "OpenGL ES" buttons, a keyboard test field, a haptics test button, two spike switches, the live log |

Environment handling in `main()`, from what stages 0–2 learned:
- `SDL_NO_SIGNAL_HANDLERS=1` (see "Signals" below);
- `ubuntumirclient` dropped from `QT_QPA_PLATFORM` (same as DolphinQt's port fix);
- **`QT_SCALE_FACTOR = GRID_UNIT_PX / 8`** when not set. Lomiri gives apps `GRID_UNIT_PX=21` on this phone (1 grid unit = 8 dp), while the Wayland output reports scale 1, so Qt Quick Controls' dp sizes need 2.625 to come out at phone size;
- **an OpenGL ES default `QSurfaceFormat`** (3.0) before the application object.

Results on the phone (screen off):
- `Qt platform 'wayland-egl', scale 2.625000`. `ubuntumirclient` was dropped, and Qt took the next entry, `wayland-egl`, which is the same Wayland plugin. The code accepts any `wayland*` name.
- **No `Cannot find EGLConfig` / `EGL error 0x3005` any more**: the OpenGL ES default format fixes Qt Quick on libhybris (stage 1's open item for QML).
- The QML launcher loads in the Material style; the app state goes `active`.

### Signals

Lomiri closes apps with SIGTERM, so the app must handle it itself.
- **SDL3 replaced the app's handlers.** When the controllers are initialized, SDL (Dolphin's controller backend) installs its own SIGINT/SIGTERM handlers (`SDL_quit.c`). Those replaced `main()`'s and turned the signals into a game stop, with the app left running. Fixed with `SDL_NO_SIGNAL_HANDLERS=1` before the controllers start.
- **`QCoreApplication::quit()` was cancelled.** In Qt 6 it first asks every window to close. The game window refuses (a close means "stop the game"), which cancels the quit. The signal handler now uses `QCoreApplication::exit(0)`, and `Emulator::Shutdown()` stops the game afterwards.
- Found from a core dump taken during the "hang" (SIGABRT, `tar -cSzf` on the phone, `gdb-multiarch` in the build container with its arm64 libraries, which are the phone's own package builds). The main thread sat idle in `QCoreApplication::exec()`.
- **Result:** SIGINT during a game quits within the launcher's window; **SIGTERM during a game stops it, releases the display lock and exits within 1 s**.

## 3. Window spike

### Option A: overlay drawn by Dolphin (built)

No `VideoCommon` change was needed:
- `Presenter::Present()` ends each frame with `OnScreenUI::DrawImGui()` and opens the next ImGui frame with `BeginImGuiFrame()`.
- `GetVideoEvents().before_present_event` fires on the video thread just before `Present()`, while that frame is open.
- `TouchOverlay` registers a hook on that event and draws its controls with ImGui primitives into `ImGui::GetForegroundDrawList()`: circles, a stick knob, labels.
- Touches on `GameWindow` are hit-tested against the same normalized layout on the UI thread and fed to `ciface::Touch::SetControlState()`, as the Android overlay does through JNI.
- `RegisterGameCubeInputOverrider(0)` or `RegisterWiiInputOverrider(0)`, chosen from the game's platform (`UICommon::GameFile`).

Layouts for the spike:
- **GameCube:** main stick, C-stick, A, B, X, Y, Z, L, R, Start, D-pad.
- **Wii Remote held sideways** (for New Super Mario Bros. Wii): D-pad, 1, 2, A, B, −, +, Home. The D-pad is rotated: the player's left is the remote's up, and the player's up is the remote's right.
- Both have an **Exit** control top left (stops the game, back to the launcher).
- Stick values follow Android: `x = dx / radius`, `y = −dy / radius`.

Results (screen off; the launcher boots the game with `--start`):

| Backend | Result |
| --- | --- |
| OpenGL ES | Runs. The overlay draws every frame: **0.19–0.28 ms of video-thread CPU per frame** (`overlay: … us per frame`, every 600 frames) |
| Vulkan | First a **crash after 12 s** (SIGTRAP from a Dolphin assert, below); fixed; then runs, 0.19–0.28 ms per frame |

**The Vulkan crash:** `Condition: final_num_bytes <= m_last_allocation_size, File: VideoBackends/Vulkan/VKStreamBuffer.cpp, Line 157, Function: CommitMemory`. With no panic handler in this frontend, the "Ignore and continue?" dialog cannot be shown and the process traps.
- Backtrace from the core: `Presenter::Present → OnScreenUI::DrawImGui → VertexManagerBase::UploadUtilityVertices → Vulkan::VertexManager::CommitBuffer → StreamBuffer::CommitMemory`.
- Instrumented: the foreground draw list **already held** 3528, 7056, 10584, 14112 … vertices when the hook started: one overlay's worth more each time.
- So with Vulkan, `before_present_event` fires for presents that end before the ImGui frame is drawn and reset (`Present()` returns early when the backend is headless, before `Finalize()` / `BeginImGuiFrame()`). The hook kept adding to the same frame until the stream buffer overflowed.
- Capping the label font size (an earlier guess: ImGui 1.92's growing glyph atlas) did not help, and plain DolphinQt with Vulkan does not crash, so it was the hook.
- **Fix:** draw at most once per ImGui frame (`ImGui::GetFrameCount()`). Both backends then run indefinitely.

**Sizes:**
- The game window first maps at 498x498 (Qt's default), then Lomiri configures it to the panel. Screen off, the configure can be landscape or portrait, as in stage 0.
- Sizes computed as logical × 2.625 were off by one or two pixels (2399x1015, 1078x2304).
- `GameWindow::PhysicalSize()` now reads the platform window's geometry (device pixels), and the log shows exactly **1080x2305**.
- The size re-send when the core reaches Running is the same as stage 2's `RenderWidget` fix.

**Not checked yet (needs the screen): what the overlay looks like, and whether touches drive the game.**

### Option B: QML overlay in a subsurface (not built)

Not built in 3.0, because option A works without any of B's risks (stage 2's subsurface stale frame and crash on Lomiri). It can still be built as a comparison if the user wants one; A is the recommendation.

### Option C: game rendered into Qt Quick (not built)

Not needed while A works. It stays the fallback if A's look (ImGui shapes now, textures later) turns out too limited.

## 4. Lomiri integration checks

| Check | Result so far | Needs the screen |
| --- | --- | --- |
| Keep the display on | `keepDisplayOn` → cookie (5, 6, …), `removeDisplayOnRequest(cookie)` → ok, on start and stop. Unconfined; a click needs the `keep-display-on` policy group | Does the screen stay on past the timeout while a game runs |
| Application state | `inactive` / `active` reported as windows change. The app pauses the game when not active and resumes on active (switch "Pause when the app is not active", on by default) | Does switching apps pause the game, and coming back resume it |
| Orientation | `reportContentOrientationChange(Landscape)` set on the game window; the screen orientation is logged. Screen off, Lomiri configured the game window landscape at first, then portrait | Does the game window stay landscape when the phone is upright |
| Launcher while playing | Hidden while the game runs (switch, on by default), shown again at stop | Does Lomiri's spread show one app instead of two |
| Haptics | First `org.kde.tactilefeedback` `HapticsEffect`: **failed on the phone (S3.0-M1)**, there is no Qt 6 feedback plugin (`qt6/plugins/feedback` does not exist). Now hfd-service over D-Bus (system bus, `com.lomiri.hfd` `/com/lomiri/hfd`, `com.lomiri.hfd.Vibrator.vibrate(i durationMs)`), as Lomiri apps get it | Does "Vibrate" vibrate (S3.0-M5) |
| On-screen keyboard | A `TextField` with maliit (`QT_IM_MODULE=maliit`) | Does the keyboard appear, and does typing reach the field |
| UI scale | 2.625 from `GRID_UNIT_PX` | Is the launcher readable and are the controls a usable size |

## Test launcher

`ut/phone-debug/run-dolphin.sh` has two new modes:
- `ut` (the QML app);
- `ut-start` (the QML app, booting the test game; add `--backend OGL` for OpenGL ES, Vulkan by default).

`ut-env.sh` now also sets Lomiri's `GRID_UNIT_PX=21`, `QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough`, `QT_QUICK_CONTROLS_STYLE=Suru` and `QT_QUICK_CONTROLS_MOBILE=1`, as apps get them.

## Manual tests (stopped here)

Screen on, phone unlocked, rotation lock off. One command:

```sh
adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 600'
```

The log stays on screen in the launcher and in `/home/phablet/dolphin-ut-user-w/run-ut.log`.

**S3.0-M1. Launcher.**
- Is the text readable and are the buttons a usable size? Yes. 
- Tap the text field: does the keyboard appear and does typing work? Yes.
- Tap "Vibrate": does the phone vibrate? No. Neither the phone nor the controller vibrates. Maybe I need to map the controller for it to vibrate (and I can't at this stage). Vibration feature of the phone is working, and I can confirm that. 

**S3.0-M2. Game with the overlay (Vulkan).** Tap "Vulkan" under the game. Then:
- the launcher should disappear, and the game should show fullscreen with round translucent controls;
- at the title screen, press **2** or **A** to start, and use the D-pad. Report whether each direction moves Mario the expected way (the sideways mapping is a guess); The mapping isn't quite right. The virtual up button is moving right, right button is down, etc. Everything else seems okay. 
- note how the controls look and whether presses feel immediate; Controls are immediate. 
- turn the phone upright and back: does the game stay landscape? Yes. 
- switch to another app and back: does the game pause and resume? (The log shows `paused (app not active)` / `resumed (app active)`.) Yes. 
- open the app spread: one Dolphin entry or two? One. 
- leave the phone untouched past the screen timeout: does the screen stay on?  Yes. 
- tap **Exit** (top left): back to the launcher? Yes.

There are 2 more things to mention. Performance is extremely good with Vulkan this time. It's extremely stable at 60fps. What did you do to fix the performance issue? (Note that 60 fps can also be achieved on Android). 

Also, the styles of the test shell isn't quite like ubuntu touch native style. I believe most ubuntu touch native apps are also Qt QML. We might need to change that later (which might only be a styling change). 


**S3.0-M3.** The same with "OpenGL ES", briefly.

Yes, everything is the same as vulkan, but the performance is worse. 

**S3.0-M4 (when the GameCube game is on the phone).** Put it in `/home/phablet/dolphin-ut-games/`, tap "Refresh" in the launcher, start it, and check the GameCube layout: the stick, A/B/X/Y, the triggers and Start.

I tried Need for Speed Most Wanted 2005, it ran at ~50-60% the speed even with Vulkan. I tried it in PAL Mode and it was slightly better. This game ran at full speed on the Android version (but if I remember correctly it was Dolphin MMJ, and it was a couple of years ago. But since GPU frequency isn't quite right, and the CPU/GPU async mode is disabled, running it at such speed actually makes sense? 2D mario is definitely less demanding than NFSMW.)

Send the terminal output and the answers. The window-architecture decision (A, or a B comparison) is then the user's, and it is recorded in `stage3-roadmap.md`.


(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 600'
running: ut: -u /home/phablet/dolphin-ut-user-w (for 600s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 600s
51:03:597 Common/HttpRequest.cpp:301 E[COMMON]: Failed to GET http://weather.wapp.wii.com/1/094/forecast.bin: server replied with code 301 and body
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb push '/home/alpha/Documents/GitHub/dolphin-ut/third_party/Need for Speed - Most Wanted.iso' '/home/phablet/dolphin-ut-games/'
/home/alpha/Documents/GitHub/dolphin-ut/third_party/Need for Speed - Most Wanted.iso: 1 file pushed, 0 skipped. 44.5 MB/s (1459978240 bytes in 31.317s)
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 180'
running: ut: -u /home/phablet/dolphin-ut-user-w (for 180s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 180s
59:00:547 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 180'
running: ut: -u /home/phablet/dolphin-ut-user-w (for 180s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 180s
02:04:692 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
02:07:347 VideoBackends/OGL/OGLConfig.cpp:744 W[Video]: Missing OGL Extensions: PrimitiveRestart PinnedMemory GSInstancing ClipControl DepthClamp 
02:07:482 VideoBackends/OGL/OGLConfig.cpp:744 W[Video]: Missing OGL Extensions: PrimitiveRestart PinnedMemory GSInstancing ClipControl DepthClamp 
02:56:086 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ 

## Results of S3.0-M1…M4 (2026-11-15)

| Check | Result |
| --- | --- |
| Launcher readable, controls usable (scale 2.625) | Yes |
| On-screen keyboard in a text field | Yes |
| Vibrate | **No**. Fixed, see below |
| Game fullscreen with the overlay, presses immediate | Yes |
| Wii D-pad direction | **Wrong**: up moved right, right moved down. Cause found, see below |
| Landscape kept when the phone is upright | Yes |
| Pause when switching apps, resume on return | Yes |
| One entry in the app spread | Yes |
| Screen stays on past the timeout | Yes |
| Exit returns to the launcher | Yes |
| OpenGL ES | Works the same as Vulkan, slower |
| Newer Super Mario Bros. (Vulkan) | A steady 60 fps |
| Need for Speed: Most Wanted (GameCube, Vulkan) | About 50–60 % speed in races; PAL slightly better |
| Style | The user wants the native Ubuntu Touch look rather than Material (a styling change) |
| Log | Only harmless messages: the WiiConnect24 weather fetch gets an HTTP 301, `Backup/SYSCONF does not exist`, and OpenGL ES lacks `PrimitiveRestart PinnedMemory GSInstancing ClipControl DepthClamp` (expected on GLES, stage 1) |

**Window architecture: option A passed every on-screen check**, with nothing left that would need B or C. Recorded as the recommendation in `stage3-roadmap.md`; the decision itself is the user's.

### The D-pad: the controller profile had a Nunchuk attached

The phone's Wii Remote profile (`dolphin-ut-user-w/Config/WiimoteNew.ini`) has `Extension = Nunchuk`, which is desktop Dolphin's default (`Wiimote::LoadDefaults`, non-Android branch). With a Nunchuk attached, New Super Mario Bros. Wii reads the remote as held upright. The overlay assumed a bare remote held sideways and rotated the D-pad (up sent the remote's right), so every direction came out 90° off, exactly as reported.

Fix (frontend only; no change to the emulated controller):
- The overlay layout now follows Wii Remote 1's configured extension, as the Android app does (`EmulatedController.getSelectedWiimoteAttachment`):
  - **Nunchuk → a new "Wii Remote + Nunchuk" layout**: the Nunchuk stick, C and Z on the left; the D-pad (not rotated), A, B, 1, 2, −, +, Home on the right.
  - No extension → the sideways layout.
- In the sideways layout, the D-pad is rotated only when the "Sideways Wii Remote" option is off. With it on, Dolphin rotates it itself, and the overlay passes the D-pad through unchanged, as on Android.
- The log line `Wii Remote 1: extension N, sideways option on/off, overlay …` shows the choice at each start.

Which extension the port's default profile has (Nunchuk as on desktop, or none as on Android) is a controller default for 3.5/3.8, and the user's call.

### Vibrate: no Qt 6 haptics backend

`HapticsEffect` loads but has nothing to drive: the phone has no Qt 6 feedback plugin. Lomiri's own path is hfd-service, already running (`/usr/libexec/hfd-service`, over `vendor.qti.hardware.vibrator.service`). A call from the shell (`gdbus call --system … com.lomiri.hfd.Vibrator.vibrate 200`) returns without an error. `LomiriIntegration::vibrate(ms)` now makes the same call (asynchronously, logging `vibrate(60 ms): ok` or the error), and the launcher's button uses it. 3.3's "haptic feedback on press" uses the same call instead of KTactileFeedback. A click needs D-Bus access to hfd in its AppArmor profile (stage 5).

### Performance: nothing was changed on purpose

Nothing was done to speed it up between stage 2 (about 45 % in DolphinQt, OpenGL ES) and now. The differences between the two setups:
- **CPU thread.** `dolphin-ut` passes no `-C`, so it runs with upstream's default for Linux, **single core** (`CPUThread=False`; the game title said `JITARM64 SC`). The stage 2 DolphinQt runs used `-C Dolphin.Core.CPUThread=True` (dual core).
- No Qt Widgets main window running beside the game, and no OSD (FPS/speed text). Both are small costs.
- The 45 % was OpenGL ES; Vulkan was already at 55–110 % in stage 2.

So single core may be at least as fast as dual core on this phone, which would change the 3.8 default. That needs measuring, not guessing. Added for it:
- a `perf:` log line every 5 s while a game runs (FPS, VPS, speed, single or dual core), taken from Dolphin's own performance metrics;
- `-C <System>.<Section>.<Key>=<Value>` on the `dolphin-ut` command line, handed to Dolphin's own parser as in `dolphin-emu-nogui`;
- **Save** and **Load** on the overlay (top left, next to Exit; state slot 1), and `--state-slot N` to load a state once the game runs. Every run can then start from the same in-game point.

A first try ran screen off for 75 s from boot. For Need for Speed it only measured the PAL intro videos and menus (100 % speed, 50 VPS, 25–50 fps), so it was not usable. The race measurement waits for a save state (S3.0-M6).

Need for Speed's 50–60 % in races goes to stage 4 along with the CPU-thread result. The likely limits are the GPU frequency (stuck low, stone-ut docs) and the CPU-thread setting. As the user notes, the old Android number came from Dolphin MMJ, a fork with its own speed hacks, a couple of years ago.

## Next manual tests (stopped here)

The build with the fixes is on the phone. Screen on, phone unlocked:

```sh
adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 900'
```

**S3.0-M5. Fixes.**
- Tap "Vibrate": does the phone vibrate? (The log shows `vibrate(60 ms): ok` or an error.)
- Start Newer Super Mario Bros. with Vulkan. The log should say `extension 1 … overlay Wii Remote + Nunchuk`, and the overlay should show a stick on the left. Do the stick and the D-pad move Mario the expected way?

**S3.0-M6. A save state for measurements.** Start Need for Speed: Most Wanted (Vulkan), play into a race, and tap **Save** (top left, next to Exit) during the race. Tap **Load** once to check that it comes back to the same point, then Exit. After that, the single-core vs dual-core measurement runs from that state over adb without touching the phone:

```sh
GAME='/home/phablet/dolphin-ut-games/Need for Speed - Most Wanted.iso' sh run-dolphin.sh ut-start 90 --state-slot 1 -C Dolphin.DSP.Volume=0 -C Dolphin.Core.CPUThread=False   # then =True
```

A save state of Newer Super Mario Bros. inside a level would allow the same comparison for a Wii game.

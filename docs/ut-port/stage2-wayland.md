# Stage 2: Wayland rendering, from upstream PR #14962

Stage 2 of `roadmap.md`. Goal: games render on screen with the hardware GPU, through both backends.

Started 2026-11-14:
- base: this fork's master at `3f9574fce5` (upstream `f9cb5f1c08` plus the stage 0/1 docs and tools);
- PR #14962 at `5003b700ffecffde62fb8fe793921512a44feb5e`, unchanged since 2026-11-07 (17 commits; checked with `gh pr view` before starting);
- phone and build environment as in `stage0-probe.md` and `stage1-build.md`.

## 1. Import

Branch `ut/wayland`, created from master. The PR is imported with

```sh
git checkout -b ut/wayland
git merge --no-ff --no-commit refs/remotes/pr/14962     # 5003b700ff
```

instead of cherry-picking the 17 commits, as the roadmap first planned. Reasons:
- `AGENTS.md` requires 🤖 in every commit message an AI agent writes. The imported commits must not be edited (roadmap, "Rules for this fork").
- The roadmap says the user commits.

With a merge, the PR's 17 commits enter history unchanged, with their authors, as the second parent of a merge commit that the user makes. The result: "Automatic merge went well; stopped before committing as requested". There were no conflicts (only `DolphinQt/MainWindow.cpp/.h` auto-merged); 61 files are staged and `MERGE_HEAD` is `5003b700ff`.

The user committed the merge on 2026-11-14: `8b7f0ebab3` "Merge remote-tracking branch 'refs/remotes/pr/14962' into ut/wayland".

The port's own changes (section 3) are still uncommitted. Suggested commits:

```sh
git add Source/Core/DolphinQt/Main.cpp Source/Core/VideoBackends/Vulkan/VKSwapChain.cpp Source/Core/DolphinQt/RenderWidget.cpp Source/Core/AudioCommon/CubebStream.h Source/Core/AudioCommon/CubebStream.cpp
git commit    # the Ubuntu Touch fixes on top of #14962 (or one commit per file)
git add docs/ut-port ut/phone-debug/run-dolphin.sh ut/phone-debug/stack-scan.py ut/phone-debug/crash-resolve.py
git commit    # notes and tools
```

When upstream merges #14962 (with rewritten history), this merge is dropped and the two fixes are rebased onto upstream (roadmap stage 2 step 8).

## 2. Build

`ut/build.sh --reconfigure` (same container and options as stage 1; `ENABLE_WAYLAND` defaults to on):

```
-- X11 support enabled
-- Found Wayland: /usr/lib/aarch64-linux-gnu/libwayland-client.so
-- Wayland support enabled
--   Found xkbcommon, version 1.6.0
-- Wayland keyboard and mouse input enabled
-- EGL OpenGL interface enabled
```

- Clean build: 2 min 40 s, no errors.
- New `NEEDED` libraries: `libwayland-client.so.0` and `libxkbcommon.so.0`, both on the phone.
- `libwayland-egl.so.1` is `dlopen`ed by `GLContextEGLWayland`.
- Version string: `Dolphin 2609-235-dirty` (the uncommitted merge).

## 3. Port fixes (unstaged, on top of the import)

Both are host-side window-system code.

**`Source/Core/DolphinQt/Main.cpp`: drop `ubuntumirclient`.**
- Lomiri gives every app `QT_QPA_PLATFORM=ubuntumirclient;wayland-egl;xcb`, and Qt 6 on the phone has that plugin. Dolphin has no window-system type for it, so it would show "Unknown Qt platform" and run headless.
- With Wayland input built (`HAVE_WAYLAND_INPUT`), Dolphin now removes any `ubuntumirclient` entry from the list before `QApplication` is created, and lets Qt take the next one. It unsets the variable if nothing is left.
- Elsewhere the variable never contains that name, so nothing changes there.

**`Source/Core/VideoBackends/Vulkan/VKSwapChain.cpp`: the swapchain size on Wayland.**
- #14962 uses the window's reported size only when `currentExtent.width == UINT32_MAX`. libhybris reports `currentExtent` 1x1 for a Wayland surface (stage 0), which would give a 1x1 swapchain.
- On `WindowSystemType::Wayland` the window's size (`m_wsi.render_surface_width/height`) is now always used. The existing clamp to `minImageExtent`/`maxImageExtent` (1x1 … 4096x4096 here) stays.

**`Source/Core/DolphinQt/RenderWidget.cpp`: send the size again once the game runs (added after S2-M1).**
- A `wl_surface` cannot report its size, so with #14962 the presenter only learns the window size from resize events (`RenderWidget` → `Host::ResizeSurface` → `g_presenter->ResizeSurface`).
- `Host::ResizeSurface` drops sizes that arrive while `g_presenter` does not exist yet, that is, while the game is starting. `RenderWidget` does not send the same size twice.
- So when Lomiri's real configure (for example landscape) arrives during startup, the picture keeps the size read at game start in `GetWindowSystemInfo()`. In a landscape window that was the earlier portrait width: the "left half of the screen" in S2-M1.
- The fix: on Wayland, when the core goes from Starting to Running (or Paused, with Boot to Pause), the widget sends its current size again. The presenter exists by then: the CPU thread reports Running only after the video backend is ready (`CPUSetInitialExecutionState`).
- It does not fire on later pause and resume, so the Vulkan swapchain is not rebuilt for nothing.

**`Source/Core/VideoBackends/Vulkan/VKSwapChain.cpp`: rebuild the whole surface, not just the swap chain, on Wayland (added after S2-M2).**
- Any swap chain re-creation (resize, vsync change, a failed present) hung the video thread forever in Vulkan. Found from a stack scan of the stuck thread (`ut/phone-debug/stack-scan.py`, symbolized with the container's `addr2line` on the unstripped `dolphin-emu`):

  ```
  Fifo::FifoManager::RunGpuLoop → AsyncRequests::PullEvents → Presenter::ViSwap → Presenter::Present
  → VKGfx::BindBackbuffer → VKGfx::CheckForSurfaceResize → SwapChain::RecreateSwapChain
  → SwapChain::CreateSwapChain → vkCreateSwapchainKHR (/android/system/lib64/libvulkan.so)
  → libhybris-platformcommon → vulkanplatform_wayland.so (WaylandNativeWindow: dequeueBuffer / readQueue)
  → libwayland-client wl_display_dispatch_queue → poll()   (never returns)
  ```

- `RecreateSwapChain()` already destroys the old swap chain before creating the new one (no `oldSwapchain`). But libhybris' `WaylandNativeWindow` keeps the destroyed swap chain's buffers until the compositor releases them, and Android's swap chain dequeues all its images when it is created. The buffer on screen is only released when a new one is committed, which cannot happen while the creation is blocked.
- The stage 0 probe created several swap chains in a row without trouble because it never presented: none of its buffers was held by the compositor.
- The fix: on `WindowSystemType::Wayland`, `RecreateSwapChain()` calls the existing `RecreateSurface()` with the same `wl_surface`. That destroys the `VkSurface` too, so libhybris creates a new native window with new buffers.
- Since the `RenderWidget` fix, every Vulkan game sends one resize as it starts, so before this fix Vulkan always hung at boot. After it, the game runs (884 frames dumped in 35 s, screen off) and stops on SIGINT.

**`Source/Core/AudioCommon/CubebStream.{h,cpp}`: do not override the restored stream volume (added after S2-M4).**
- With `media.role=multimedia` (the launcher's `PULSE_PROP`), PulseAudio's `module-stream-restore` gives Dolphin's stream the stored multimedia volume, the one the volume keys and the sound indicator change.
- At startup (`AudioCommon::PostInitSoundStream` → `UpdateSoundStream`), Dolphin set the stream to its own volume setting, 100 % by default (`cubeb_stream_set_volume` → `pa_context_set_sink_input_volume`; the sink is not flat-volume, so cubeb changes the stream's volume).
- `module-stream-restore` then **saved** that 100 % as the multimedia role volume, and the indicator followed (AccountsService `com.lomiri.AccountsService.Sound` `Volume` read 1.0). So every launch started at full volume and reset the media volume to full: the "always at max" the user reported.
- The fix: `CubebStream::SetVolume()` leaves the stream alone until Dolphin's volume first differs from 100 %. A new cubeb stream already plays at full scale, so on other systems the first call was a no-op anyway. Once the user changes Dolphin's own volume, it is applied as before.
- Checked (speaker sink muted for the test):
  - before the fix, Dolphin's stream was at 100 % in the multimedia role;
  - with the multimedia volume set to 30 % (on a silent `pacat` stream), the fixed Dolphin started at 30 %, and the indicator showed 0.3 afterwards;
  - **the media volume was left at 30 %**.

Not changed:
- Dolphin's surface format choice already takes `R8G8B8A8_UNORM` (this surface has no BGRA).
- `VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR` is accepted by libhybris (stage 0).

## 4. On the phone (automated, screen off)

Deployed with `ut/build.sh --package`, a push of the tarball, and `tar xzf` into `/home/phablet/dolphin-ut-arm64/`. Test user folder: `/home/phablet/dolphin-ut-user-w`. Every run started with the session's own `QT_QPA_PLATFORM=ubuntumirclient;wayland-egl;xcb`, plus `ut-env.sh` and the unpacked xdg-shell plugin (`ut_qt6_wayland`).

### OpenGL ES through Qt's Wayland window

`dolphin-emu -u … -b -v OGL -C Dolphin.Core.CPUThread=True -e <game>` (batch mode: no main window, only the render window).

Mapped in the running process:

```
/usr/lib/aarch64-linux-gnu/qt6/plugins/platforms/libqwayland.so
…/qt6w/…/wayland-shell-integration/libxdg-shell.so
/usr/lib/aarch64-linux-gnu/libwayland-egl.so.1.22.0
/usr/lib/aarch64-linux-gnu/libhybris/eglplatform_wayland.so
```

and no `ubuntumirclient` or `libqxcb`. The process environment still shows the inherited `ubuntumirclient;wayland-egl;xcb`, so the fix in `Main.cpp` is what made Qt pick Wayland.

Log:

```
Common/Wayland/ContentType.cpp:64 I[COMMON]: The Wayland compositor does not support content type hints
I[BOOT]: CPU Thread separate = Yes
Common/GL/GLInterface/EGL.cpp:80 I[Video]: Error: couldn't get an EGL visual config      (the desktop-GL probe, as in stage 1)
Common/GL/GLInterface/EGL.cpp:117 I[Video]: Using OpenGL|ES
I[Video]: Video Info: Qualcomm, Adreno (TM) 619, OpenGL ES 3.2 V@0530.0 …
W[Video]: Missing OGL Extensions: PinnedMemory GSInstancing ClipControl DepthClamp
I[Audio]: Cubeb initialized using pulse backend
N[BOOT]: Booting from disc … Apploader Initialized … I[AI]: Audio DMA configured
W[Video]: org.freedesktop.ScreenSaver::Inhibit failed: No such method 'Inhibit' …
I[Video]: Could not inhibit screensaver: No services available
```

- **#14962's EGL-on-Wayland context runs on the Adreno through libhybris**, and Dolphin picks GLES by itself.
- **The missing content-type protocol is handled**: one info line, nothing else.
- The game boots and runs in dual-core mode, with audio.
- **Screensaver inhibit fails** on Ubuntu Touch (`org.freedesktop.ScreenSaver` has no `Inhibit` here). During play the display may dim and turn off. That is stage 3 (keep-display-on through Lomiri's own interface).

### Vulkan through Qt's Wayland window

Same, with `-v Vulkan`:

```
I[Video]: Enabling extension: VK_KHR_get_physical_device_properties2
I[Video]: Enabling extension: VK_KHR_surface
I[Video]: Enabling extension: VK_KHR_wayland_surface
I[Video]: Enabling extension: VK_KHR_get_surface_capabilities2
I[Video]: 1 vulkan queue families
I[Video]: Enabling extension: VK_KHR_swapchain
I[Video]: Loaded 0 cached shaders … (Vulkan-uber-vs/ps, specialized-vs/ps)
… game boots, audio DMA configured …
I[CONSOLE]: Stop [Main Thread 0]  Wait for Video Loop to exit …
I[CONSOLE]: Stop [Video Thread 0] Video Loop Ended … HW shutdown
```

No Vulkan error or warning. The Wayland surface, swapchain and rendering came up, and stopping was clean. Dolphin does not log the swapchain size, so whether the 1x1 fix gives a full-size picture is checked on screen (S2-M3).

### "Hang" on stop: Confirm on Stop

The first runs did not exit after SIGINT. Dolphin printed "A signal was received. A second signal will force Dolphin to stop.", stopped audio, and then waited: the main thread idle in its event loop (`do_sys_poll`), the CPU and video threads in `futex` waits.
- It happened with every backend, including `-v Null` (no GL at all), with vsync off, and in single-core mode. So it was not the renderer or a Wayland swap.
- Cause: the Qt UI's **Confirm on Stop** (on by default). The stop request opens a confirmation dialog and pauses emulation until someone answers. With the screen off, nobody could.
- With `-C Dolphin.Interface.ConfirmStop=False`, Dolphin exits 3–4 s after SIGINT (OpenGL and Vulkan).
- For stage 3: how the app stops and pauses on a phone (Lomiri closing or suspending the app, the dialog) needs a decision.

Test matrix (each 20 s, then one SIGINT), using a scratch script pushed as `/home/phablet/dolphin-ut-probe/stoptest.sh` (not kept in the repository):

| Run | Stop |
| --- | --- |
| `-v Null`, dual core | waited (dialog) |
| `-v OGL`, dual core, vsync off | waited (dialog) |
| `-v OGL`, single core | waited (dialog) |
| `-v OGL`, dual core | waited (dialog) |
| `-v OGL`, dual core, `ConfirmStop=False` | exited in 4 s |
| `-v Vulkan`, dual core, `ConfirmStop=False` | exited in 3 s |

### Test launcher

`ut/phone-debug/run-dolphin.sh` (pushed to `/home/phablet/dolphin-ut-probe/`) runs Dolphin the way a click app would. It sources `ut-env.sh`, adds the xdg-shell plugin, and sets the session's `QT_QPA_PLATFORM`. It always passes dual core, the FPS and speed OSD, and the test user folder. It stops Dolphin after N seconds and prints errors and warnings from the log.

| Mode | What |
| --- | --- |
| `ogl`, `ogl-fs` | the first game in `/home/phablet/dolphin-ut-games/`, batch mode, OpenGL ES, windowed or fullscreen, Confirm on Stop off |
| `vk`, `vk-fs` | the same with Vulkan |
| `ui` | the main window (the game list reads `/home/phablet/dolphin-ut-games/`), a separate render window |
| `ui-main` | the main window with Render to Main Window |

Usage: `sh run-dolphin.sh MODE [SECONDS] [extra dolphin args]`. The log is `/home/phablet/dolphin-ut-user-w/run-MODE.log`. Logging in that user folder is set to INFO for BOOT, CORE, COMMON, CONSOLE, Video, Audio and JIT, written to the console.

## Manual tests

These need the screen on and the phone unlocked. Run each from the PC; each prints a short error summary at the end. Rotation lock should be off for the rotation checks.

**S2-M1. OpenGL ES, windowed, then fullscreen.** Done 2026-11-14 (user), with the build before the RenderWidget fix.

```sh
adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ogl 90'      # stopped after 90 s by the launcher
adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ogl-fs 90'   # "exited by itself after 88s (status 0)"
```

User report:
- **The game appears and looks right.** Colors are good, so the washed-out colors seen with #14962 on KDE do not show up here.
- **Speed is about 45 %** (the OSD's speed figure) on this screen and the title. That is a stage 4 item. Candidates: the GPU clock (stage 1 saw 266 MHz at 100 % busy), the two compositors, thread placement.
- **Fullscreen covers the whole screen**, including the top panel.
- **Rotation: the picture follows.**
- **Bug:** in the windowed run, if the phone was already in landscape when the game started, the picture covered only the left half of the screen. Rotating to portrait and back fixed it. Cause and fix: section 3, `RenderWidget.cpp`. To re-check in S2-M2 and S2-M3.
- **Bug, fixed:** the volume keys did not change Dolphin's volume, and it was loud enough to risk the speaker. See "Volume keys" below.

Logs: only the usual `Missing OGL Extensions: PinnedMemory GSInstancing ClipControl DepthClamp`.

The fullscreen run ended on its own at about 86 s with a clean shutdown and no signal in the log (`---- Shutting down ----` right after the screensaver warning). In batch mode that is what closing the render window does: a swipe-away, or Lomiri closing the window. Which one it was is to be confirmed.

#### Volume keys (fixed in the launcher)

- Ubuntu Touch's PulseAudio keeps volumes per media role: `module-stream-restore` with `fallback_table=/etc/pulse/touch-stream-restore.table` (multimedia −8, alert −8, alarm −8, phone −15), plus `module-role-cork` and `module-role-ducking`. The volume keys and the sound indicator change the **multimedia** role.
- Dolphin's stream (Cubeb, PulseAudio backend; `media.name = "Dolphin Audio Output"`) has **no `media.role`**. stream-restore keys it as `sink-input-by-application-name:Dolphin Emulator`, so the volume keys never touch it.
- Fix without code changes: libpulse adds the `PULSE_PROP` environment variable to the client's properties, and the stream inherits them. With `PULSE_PROP="media.role=multimedia"`, the same stream shows `media.role = "multimedia"` and `module-stream-restore.id = "sink-input-by-media-role:multimedia"`, so it takes the multimedia volume the keys control. It is also corked during calls and alarms (`module-role-cork`) and ducked under alerts, like other media.
- Checked with Dolphin's own volume at 5 % (`-C Dolphin.DSP.Volume=5`) to keep the speaker safe.
- `ut/phone-debug/run-dolphin.sh` now exports it. The click launcher must too (roadmap stage 5).

**S2-M2. Vulkan, windowed, then fullscreen.**

```sh
adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh vk 90'
adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh vk-fs 90'
```

Report the same. In particular: is the picture full-size? (If the swapchain fix did not work, the picture would be a single stretched pixel or blank.)

Result (2026-11-14, user, build `2609-253-dirty`): **failed.**
- `vk` and `vk-fs`: black on the left of the screen, wallpaper on the right. Neither run stopped on SIGINT (the launcher had to send the second signal), although Confirm on Stop was off.
- The logs end at "Setup Wii Memory…" plus the screensaver warning: the game never got past boot.
- Cause: the Vulkan swap chain re-creation hang (section 3, `VKSwapChain.cpp`, the second fix). The `RenderWidget` fix makes every game send a resize as it starts. In Vulkan that resize rebuilt the swap chain on the same libhybris window, and the video thread blocked in `poll()` inside libhybris' `vulkanplatform_wayland.so`. The CPU thread then waited on the video thread, so the game froze on the first (black, too small) frame, and SIGINT could not stop it.
- Reproduced with the screen off. Fixed by rebuilding the `VkSurface` on Wayland. **To re-test** with build `2609-253-dirty` + this fix (deployed 2026-11-14).

The re-checks from S2-M1 **passed** (user): the volume keys now control Dolphin, and a game started in landscape fills the window (OpenGL ES).

**Re-test with the surface fix (2026-11-14, user): passed.** Both `vk` and `vk-fs` work. Speed was clearly better than OpenGL ES in S2-M1: **55–110 %** of full speed (the OSD's speed figure), against about 45 %. That goes into stage 4's backend comparison.

**S2-M3. The main window on native Wayland, separate render window.**

```sh
adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ui 180'
```

First run (build `2609-253-dirty`, before the surface fix): failed at game start. The main window appeared natively (no Xwayland) with the game in the list, but starting the game gave the black-left / wallpaper-right picture and the app was stuck. With no `-v`, the default backend is Vulkan, so it was the S2-M2 hang.

**Re-test with the surface fix (2026-11-14, user):**

| Check | Result |
| --- | --- |
| Main window on native Wayland, game in the list | yes |
| Double tap starts the game | yes |
| Separate render window shows the game | yes. In Lomiri's app spread it shows up as **two apps** (each top-level window is one entry) |
| Full Screen toolbar button | **no: the button was grayed out** (see below) |
| Stop, with the confirmation dialog | yes |
| Tooltips and menus | yes |

Also reported (user):
- **Qt windows have no title bars.** That is by design here: the launcher (like every Lomiri app) sets `QT_WAYLAND_DISABLE_WINDOWDECORATION=1`, and Lomiri's phone mode does not move windows anyway.
- **The Wii Remote mapping window is larger than the screen.** Its close button is outside the screen edge, and it cannot be moved or closed (no title bar, no keyboard for Esc). The GameCube mapping window fits and works: the user remapped the GameCube controller successfully.
  - **Deferred, not fixed:** this is the desktop Widgets UI, which the QML frontend replaces (roadmap stage 6, user decision). For testing until then: use a GameCube game (the user is providing one), open the Wii mapping window in landscape (2400 px wide), or edit `Config/WiimoteNew.ini` directly.

#### The grayed Full Screen button: investigation

The fullscreen toggle itself works. It was tested with the screen off and no taps: Dolphin's named-pipe input backend (built in: "Using named pipes as controller inputs") was used to press the **Toggle Fullscreen hotkey** from the shell. The scratch script is `/home/phablet/dolphin-ut-probe/fstoggle.sh` (not kept in the repository). It:
- creates the FIFO `User/Pipes/hk`;
- writes `Config/Hotkeys.ini` with `Device = Pipe/0/hk` and ``General/Toggle Fullscreen = `Button A` ``;
- starts the UI with the game, `HotkeysRequireFocus=False` and `ConfirmStop=False`;
- sends `PRESS A` / `RELEASE A` three times, 8 s apart, with `WAYLAND_DEBUG=1`.

The hotkey calls the same `MainWindow::FullScreen()` as the toolbar button.

- **Separate render window (`ui`):** each press sent `xdg_toplevel.set_fullscreen` / `unset_fullscreen` on the render window's toplevel. Lomiri answered with a fullscreen configure of 2400x1080 (the whole panel in landscape) and went back to 2400x1017 (below the panel) on unset. Dolphin stayed alive, with no protocol error and no log error.
- **Render to Main Window (`ui-main`):** each press moved the render surface from a `wl_subsurface` of the main window to its own `xdg_toplevel` with `set_fullscreen`, and back (`wl_subsurface.destroy` → `get_toplevel` → `xdg_toplevel.destroy` → `get_subsurface`). It happened twice, and Dolphin stayed alive. **No `wl_display.error`**: the "surface already has a role assigned wl_subsurface" error #14962 once hit on Sway does not happen here. Some configure sizes (2400x1017 while fullscreen) are unreliable with the screen off, as in stage 0, so on-screen sizing is still a user check.
- **The button:** `ToolBar::OnEmulationStateChanged()` enables Full Screen with exactly the same condition as Stop (`state != Core::State::Uninitialized`). No other code disables it: no `setEnabled` on it anywhere else, in upstream or in #14962.
- The main window was not showing a stale picture either. Its `wl_surface` commits a new buffer right after the game starts (just after the render window's title changes to the running-game title) and again around each toggle.
- **Not a bug (user, 2026-11-14):** the separate render window and the main window are two apps in Lomiri. The right sequence is: start the game, switch to the main window, press Full Screen, switch back to the game, which is then fullscreen. **Fullscreen works in this mode.**
- (Earlier guess, kept for the record: the button looked grayed because no game was running.) To check again: with the game running, switch to the main window in the spread, and compare Full Screen with Stop. **Emulation → Toggle Fullscreen** in the menu bar does the same thing, with the same enable rule (`MenuBar.cpp`).

**S2-M4. Render to Main Window.**

```sh
adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ui-main 180'
```

**Result (2026-11-14, user): crash on fullscreen.** Everything worked until Full Screen was pressed. Then Dolphin died (`exited by itself after 18s (status 139)`, SIGSEGV). Dolphin's fault handler logged:

```
Core/PowerPC/JitArm64/Jit.cpp:130 E[JIT]: Exception handler - Not on CPU thread
R0: 0x0000006b6c0022f0  R1: 0x0  R2: 0xffffffffffffffd8  R3: 0x0000006b6c002320 …
R19: 0x0000006b6c0022f0 … R29: 0x0000006b85e5af90  R30: 0x0000007fbd526d70  SP: 0x0000006b85e5af90
Access Address: 0x0000000000000018
PC: 0x0000007fbd526d78
```

- A null-pointer dereference (offset `0x18`) on a thread that is not the emulated CPU's, in a shared library (PC and LR are 8 bytes apart, in the `0x7fb…` library range). The log does not say which library; no memory map or core was captured in that run.
- The automated toggle test (screen off, pipe hotkey) did **not** crash in the same mode: the render surface moved between `wl_subsurface` and fullscreen `xdg_toplevel` twice without errors.
- **Working hypothesis, not confirmed:** Render to Main Window is the only case where the fullscreen toggle destroys the `wl_surface` the video backend renders into. The render widget moves between a subsurface and a top-level window, and Qt replaces its platform window. #14962 pauses presentation around the switch (`Host::RunWithPresentationPaused`), but the backend's old window (libhybris' `WaylandNativeWindow` behind the `VkSurface`, or the `wl_egl_window`) stays alive until the GPU thread handles the surface change afterwards. With the screen on, the compositor still sends frame and buffer-release events for the destroyed surface, and libhybris' handlers can then follow freed or null state. With the screen off, those events do not come, which would explain why the automated test passed. Android Dolphin avoids this order by handing the backend `ChangeSurface(nullptr)` and pausing before its surface is destroyed (`MainAndroid.cpp`, `SurfaceDestroyed`).
- **Worth fixing?** For this Widgets UI, a workaround exists: use the separate render window (`ui`), where fullscreen works. But the QML frontend (stage 6) will host the game picture inside its own window and needs the same "surface replaced while the game runs" case to be safe. So it is worth finding the cause now, rather than guessing a fix.
- **Crash capture added to the launcher** (`run-dolphin.sh`, pushed):
  - it saves Dolphin's `/proc/PID/maps` every 2 s (`run-MODE.maps`);
  - on a crash it runs `crash-resolve.py`, which places the logged PC and LR in a library with a file offset;
  - it enables core dumps of private memory (`coredump_filter` `0x11`). A core is about 400 MB on disk (sparse, about 70 GB apparent size, because of fastmem's address reservation) and lands in `/home/phablet/dolphin-ut-arm64/core`; delete it after use.
  - Checked with a SIGABRT: the launcher reported the crash and wrote the core. (A SIGSEGV sent with `kill` is ignored by Dolphin's fault handler, because it is not a real fault.)
- **Re-run needed** (user): the same steps. Start the game in `ui-main` and press Full Screen. The launcher prints the resolved PC and LR. Send the output; the maps file and core stay on the phone.

**Re-run with crash capture (2026-11-14, user): no crash in two runs**, so the crash is intermittent. The capture stays in the launcher; if it happens again, the resolved PC and LR come with it. Two other observations, both analyzed below:

1. **Fullscreen with Render to Main Window shows up as a separate app** in Lomiri's app spread, and stopping needs a switch back to the main window. **Expected; not a port bug.** This is how DolphinQt does fullscreen in this mode: the render widget leaves the main window and becomes its own top-level window (`MainWindow::FullScreen`; the trace shows `wl_subsurface.destroy` → `xdg_surface.get_toplevel` → `set_fullscreen`). Lomiri lists every top-level window. An in-game menu (the user's idea: the Android app's in-game overlay menu) is QML frontend work (stage 6).

2. **After Stop, the main window kept a black area with the frozen FPS counter, and "never stopped".**
   - The emulation did stop. The user's log shows `---- Shutting down ----` at 25:26:424 and `---- Shutdown complete ----` 89 ms later. Dolphin then stayed responsive: it handled the launcher's SIGINT at 60 s and exited normally.
   - Reproduced headless with pipe hotkeys (`/home/phablet/dolphin-ut-probe/fsstop.sh`, scratch: fullscreen at 20 s with button A, Stop at 30 s with button B, `WAYLAND_DEBUG=1`). The client side does everything right:

     ```
     xdg_toplevel@25.destroy()                       leaving fullscreen, as part of Stop
     wl_subcompositor.get_subsurface(wl_subsurface@24, wl_surface@54, main wl_surface@17)   back in the main window
     wl_surface@54.attach/commit … (last game frame at 1388071 ms)
     ---- Shutting down ---- / ---- Shutdown complete ----
     wl_subsurface@24.destroy()                      1388743 ms
     wl_surface@54.attach(nil) / commit / destroy    1388744 ms
     wl_surface@17.attach(wl_buffer@51) / commit     1388822 ms  (the main window redraws: the game list)
     ```

   - So the render subsurface is unmapped and destroyed, and the main window commits a new frame, within 0.1 s of the stop. A frozen last game frame on screen after that is **Lomiri (Mir 1.8) still showing a destroyed subsurface's last buffer**: a compositor issue, not one in Dolphin. (Like the crash, it only happens in this mode, which is the only one that puts the game on a `wl_subsurface`.)
   - Not confirmed on screen yet. An easy check, optional: when it happens, switch apps or rotate the phone. If the stale frame disappears on the next recomposite, it is the compositor.

**Decision for Render to Main Window** (recommendation, the user decides):
- **Do not fix it in this port; avoid it.** On Lomiri, the game should not be drawn into a `wl_subsurface`. That mode's three problems (the intermittent crash on fullscreen, the stale frame after Stop, two apps in the spread) all come from subsurfaces and from moving the render surface in and out of the main window. The separate render window (S2-M3) works, including fullscreen and Stop.
- The port's default settings (stage 3) should keep `RenderToMain = False`, which is also upstream's default.
- For the QML frontend (stage 6): give the game its own top-level surface. Draw on-screen controls into the game picture itself (Dolphin's on-screen UI, ImGui), rather than as Qt widgets in a subsurface over it.

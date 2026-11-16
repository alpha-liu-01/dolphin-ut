# Stage 3.2: emulation screen and lifecycle

Goal (`stage3-roadmap.md`): start, run, pause and stop a game the phone way.

Status (2026-11-15): **done** (re-test passed: fullscreen with the panel hidden upright and in landscape, still fullscreen after an app switch, the library back with its panel after Exit, readable statistics).

Most of it was already built and tested on screen in 3.0. 3.2 adds:
- Back/Escape handling;
- the FPS and speed display switch;
- a suspend test;
- an investigation of the top panel in landscape.

On-screen tests are below.

## The plan's steps, checked

| Step | State | Where / evidence |
| --- | --- | --- |
| 1. Boot from the library through `BootManager` with the game window's `wl_display` / `wl_surface` and size | Done (3.0, 3.1) | `Emulator::start`, `MakeWindowSystemInfo`; S3.1-M2 |
| 2. Resize and rotation into `g_presenter`; re-send the size once the game runs | Done (3.0) | `OnGameWindowSizeChanged`, the Starting→Running re-send; exact sizes in the log |
| 3. Landscape while playing; fullscreen, no chrome, panel hidden | Landscape: done (3.0, S3.0-M2). Fullscreen: requested; **the panel question is open** (below) | `reportContentOrientationChange`, `showFullScreen` |
| 4. Keep the display on | Done (3.0, S3.0-M2) | `LomiriIntegration::SetKeepDisplayOn` |
| 5. Lifecycle: pause when inactive, audio off, resume, survive suspension, close cleanly | Pause and resume: done (3.0, S3.0-M2). Audio: stops with the pause. Suspension: tested (below). Close: below | `OnApplicationStateChanged` |
| 6. Back / Escape opens the in-game menu | Built; until the menu exists (3.4), a short press pauses or resumes, a long press stops | `GameWindow::keyPressEvent` / `keyReleaseEvent` |
| 7. FPS and speed display as an option | Built: Dolphin's own on-screen statistics, a switch on the Developer page until the settings screens (3.6) | `Emulator::showPerformanceStats` |

## Lifecycle details

### Pause stops the audio

`Core::SetState(Paused)` puts the CPU in stepping mode. The CPU then turns the sound stream off (`CPU.cpp` → `AudioCommon::SetSoundStreamRunning(false)`), and turns it on again on resume. Nothing extra is needed for "stop audio when inactive".

### Suspension (SIGSTOP / SIGCONT)

Lomiri freezes background apps (SIGSTOP) and thaws them (SIGCONT), unless they are exempt. An app started over adb is not managed, so the test simulated it. The harder case was used: frozen while running, not paused.
- Need for Speed from its race save state, dual core, Vulkan.
- SIGSTOP 20 s in, SIGCONT 15 s later.

Result: emulation and rendering carried on right after SIGCONT (`perf:` lines at 32–52 %, as before). There was no `VK_ERROR_DEVICE_LOST`, no GPU fault in the kernel log, the PulseAudio stream continued, and the stop at the end was clean.

In normal use the game is already paused (the app goes inactive) before Lomiri freezes it. Mir keeps the same `wl_surface` across suspension, so nothing needs recreating.

### Closing

- **Swiping the app away in the spread:** Lomiri closes the app's windows.
  - A close on the game window stops the game (`CloseRequested`).
  - A close on the library window ends the app (Qt quits on the last window), and `Emulator::Shutdown()` then stops any game.
  - SIGTERM, which a click gets when it is stopped, quits within about 1 s (3.0).
  - Nothing is saved implicitly (no automatic save state).
- **Exit** on the overlay: stops and returns to the library (3.0).

### Back / Escape

As the Android app (`EmulationActivity`):
- **Back** opens or closes the in-game menu;
- **a long press** (500 ms) stops the game.

Until 3.4 builds the menu, a short Escape or Back pauses or resumes, logged as `menu requested (no in-game menu yet): pause/resume`. A long press stops.

Ubuntu Touch has no system Back gesture, so this needs a keyboard. A gamepad's Back/Select goes to Dolphin's controller input (SDL), not to Qt. "Open the menu" from a gamepad is a hotkey question for 3.4/3.5.

## The top panel in landscape (fixed after S3.2-M1; see the results)

The game window asks for fullscreen (`xdg_toplevel.set_fullscreen`, twice, before it is mapped). In the adb tests, with the phone lying flat (the shell in portrait), Lomiri gave:
- **1080x2400** first: the whole screen, in portrait;
- then **2400x1017** once the window turned landscape (the content orientation hint). The 63 px missing are 3 grid units, the height of Lomiri's top panel. The library window gets the same 2400x1017.

A test asked for fullscreen again once the window was landscape. Lomiri acknowledged it (the configure gained the fullscreen state: `array[8]` against `array[4]`) but kept 2400x1017. In stage 2, DolphinQt's render window held in landscape got 2400x1080.

So the shell's own orientation probably decides this. The phone lying flat (shell in portrait) with the app turned to landscape leaves a strip; the phone held in landscape should not. The re-request was removed (it changed nothing), and the on-screen test below checks the real case.

## FPS and speed display

Dolphin's own statistics (`Graphics.Settings.ShowFPS` and `ShowSpeed`, top right, drawn by its on-screen UI like the overlay).
- The Android app has them in Graphics → Statistics (3.6).
- For now: a switch on the Developer page, "Show FPS and speed in games", saved in `GFX.ini`.

## Not changed here

- Dual core and the CPU boost are decided defaults, but they are applied in 3.8 (and the boost is built in 3.6). Until then, `dolphin-ut` runs single core unless `-C Dolphin.Core.CPUThread=True` is given.

## Manual tests

Screen on, unlocked, rotation lock off. Start the app normally:

```sh
adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 900'
```

**S3.2-M1. Fullscreen and rotation.** Start Need for Speed from the library.
- Holding the phone upright: is Lomiri's top panel (clock, indicators) visible as a strip along one long edge of the game, or does the game cover the whole screen?
- Turn the phone to landscape, then to the other landscape side. Does the game cover the whole screen, and does it follow the phone, or stay one way?
The orientation is correct in portrait and landscape, but the top panel IS visible both ways (not just a black strip, WiFi icons and such are also present). It failed to enter fullscreen. 

**S3.2-M2. Switching away.** While playing, switch to another app for a few seconds and back. The game should be paused, and silent, while away, and resume on return.
Works as expected.

**S3.2-M3. Screen lock.** While playing, press the power button, wait a few seconds, then wake and unlock. Paused while off, resumed after unlocking?

Works as expected.


**S3.2-M4. FPS and speed.** Exit, open ⋮ → Developer, turn on "Show FPS and speed in games", start a game. Do the statistics show (top right), and do they clash with the touch controls? Turn the switch off again afterwards if you prefer.

Works as expected, mostly. The FPS statistics are too small though. 


(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 500'
running: ut: -u /home/phablet/dolphin-ut-user-w (for 500s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 500s
07:59:317 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
08:04:063 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
08:12:193 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
08:19:910 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
08:35:717 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
08:35:717 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (WaitForCommandBufferCompletion:286) vkWaitForFences failed:  (2: VK_TIMEOUT)
08:35:717 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
08:35:717 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (WaitForCommandBufferCompletion:286) vkWaitForFences failed:  (2: VK_TIMEOUT)
08:35:719 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
08:35:720 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
08:35:724 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
08:35:728 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
08:35:734 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
08:35:741 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
08:35:754 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
08:35:754 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (WaitForCommandBufferCompletion:286) vkWaitForFences failed:  (2: VK_TIMEOUT)
08:35:755 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
08:35:755 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (WaitForCommandBufferCompletion:286) vkWaitForFences failed:  (2: VK_TIMEOUT)
08:35:755 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
08:35:755 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (WaitForCommandBufferCompletion:286) vkWaitForFences failed:  (2: VK_TIMEOUT)

Those issues are the same one running Wind Waker with Vulkan before. You can ignore that for now. The rest of the tests are done with OpenGLES Wind Waker. 

**S3.2-M5. Closing from the spread.** While a game runs, open the app spread and swipe Dolphin away. The app should close; then start it again from the terminal command, and it should work normally.

I don't think I can test it correctly with this command. 
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 500'
running: ut: -u /home/phablet/dolphin-ut-user-w (for 500s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
Maybe I should adb shell first and run the command `sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 500` in adb shell so that I can kill it. 
And it does work after I test it this way. 
phablet@ubuntu-phablet:~$ sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 50 
running: ut: -u /home/phablet/dolphin-ut-user-w (for 50s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
^C
phablet@ubuntu-phablet:~$ sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 50
running: ut: -u /home/phablet/dolphin-ut-user-w (for 50s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
^C
phablet@ubuntu-phablet:~$ 

**S3.2-M6 (only with a keyboard).** If a Bluetooth or USB keyboard is at hand: in a game, a short Escape pauses and resumes; holding Escape stops the game.

USB keyboard works in both cases. Bluetooth keyboard failed to input anything in ubuntu touch even though it was connected. Maybe it's my issue when I was doing the port of ubuntu touch to this device. You can ignore that for now and I'll deal with this issue in the stone-ut repo. 

Send the terminal output and the answers.


## Results of S3.2-M1…M6 (2026-11-15)

| Test | Result |
| --- | --- |
| M1 Fullscreen and rotation | Orientation right upright and in landscape. **The top panel stayed visible both ways (clock and indicators): not fullscreen.** Fixed (below) |
| M2 Switching away | Paused and silent while away, resumed on return |
| M3 Screen lock | Paused while off, resumed after unlocking |
| M4 FPS and speed | Shown, no clash with the controls, but **too small**. Fixed (below) |
| M5 Closing from the spread | The app closes; started again from an interactive `adb shell`, it works normally |
| M6 Escape | With a USB keyboard: a short press pauses and resumes, a long press stops. A Bluetooth keyboard typed nothing anywhere in Ubuntu Touch; a stone-ut issue, the user is following it there |

Also seen: Wind Waker on Vulkan hit the known GPU hang (`stage3.1-library.md`); the rest was tested with OpenGL ES.

### Fullscreen: two conditions on Lomiri

`WAYLAND_DEBUG` traces, using the size Lomiri hands out as the measure (portrait shell: 1080x2305 with the panel, 1080x2400 without):

| Variant | Lomiri's answer |
| --- | --- |
| 3.0–3.2: `set_fullscreen` before the window is mapped (Qt's `showFullScreen()` on a new window) | Ignored: configures carry only "activated" (`array[4]`), size 1080x2305. Same without the orientation hint, so the hint is not the cause |
| DolphinQt (`ogl-fs`, reference): mapped normally, then `set_fullscreen` | Fullscreen state (`array[8]`), then 1080x2400 |
| `set_fullscreen` after mapping, with the library window still showing | Fullscreen state, but the size stays 1080x2305 |
| **`set_fullscreen` after mapping, the library window hidden first** | **Fullscreen state and 1080x2400** |

So Lomiri (Mir 1.8) needs both of these:
- **the request after the window is mapped**;
- **no other window of the app showing**. DolphinQt's batch mode never shows its main window, which is why it worked there.

Now:
- `GameWindow` is shown normally and calls `showFullScreen()` on its first expose;
- `Emulator::start` hides the library window before showing the game window.

With the Developer switch "Hide this window while playing" off, the panel stays. That switch is a 3.0 spike leftover.

### FPS and speed too small

Dolphin sizes its on-screen UI (statistics, messages) by `WindowSystemInfo::render_surface_scale`. `dolphin-ut` filled it with Qt's device pixel ratio, which was 2.625 in 3.0 and became 1 when 3.1 dropped `QT_SCALE_FACTOR` for Lomiri Components. It is now Lomiri's grid-unit scale, `GRID_UNIT_PX / 8` = 2.625 (`LomiriIntegration::GridUnitScale()`), as in 3.0. The touch overlay is not affected: it sizes itself from the window.

## Re-test

The fixed build is on the phone. Run it from an interactive `adb shell` (as the user did for M5), so Ctrl+C works:

```sh
sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 900
```

**S3.2-M1b.** Start Need for Speed (or Wind Waker with OpenGL ES) from the library:
- is the top panel gone, upright and in landscape, with the picture the right way round?
Yes.
- switch to another app and back: still fullscreen?
Yes.
- Exit: is the library back as before, with its panel?
Yes. 

**S3.2-M4b.** With "Show FPS and speed in games" on: are the statistics a readable size now?
Yes. 

# Stage 4: performance and correctness on Adreno 619

Goal (from `roadmap.md`, narrowed 2026-11-16): know what runs well, fix or work around what does not, and decide on custom GPU drivers. Stage 3 already settled speed and defaults (CPU boost, dual core, Vulkan with an OpenGL ES fallback).

## Step 1: Wind Waker hangs the GPU on Vulkan

### Reproduced

Today's build, `run-dolphin.sh ut-start` with `--backend Vulkan` (the test folder has a per-game OpenGL ES setting for the game):
- About 11 s in, at the title screen, the kernel logs `kgsl kgsl-3d0: MISC: GPU hang detected` and `gpu fault ctx … ctx_type VK` from `dolphin-ut`. Then every `vkQueueSubmit` fails with `VK_ERROR_DEVICE_LOST`.
- The emulation keeps running at full speed, but nothing reaches the screen. The alert handler (3.6) logged and showed **7,647 alerts** in 40 s.

### Cause: dual-source blending on this driver

Bisected with a temporary diagnostic, `ut/phone-debug/vk-diagnostic.patch`. It is not for commit, and the source was reverted after each build. It adds two switches:
- `DOLPHIN_UT_VK_DISABLE=dualsrc,bbox,…` switches optional Vulkan features off;
- `DOLPHIN_UT_VK_BUG_DSB=1` marks dual-source blending broken (`DriverDetails::OverrideBug`).

The features Dolphin uses on this driver (V@530, `driverVersion` `0x80212000`):

| Feature | On this driver |
| --- | --- |
| dual-source blending, bounding box (fragment stores and atomics), sample-rate shading, depth clamp, large points, palette conversion, dynamic sampler indexing, LOD bias in sampler | on |
| geometry shaders | off (no `shaderTessellationAndGeometryPointSize`) |
| primitive restart, reversed depth range | off (Dolphin's existing Qualcomm entries) |

Runs of Wind Waker from boot, 30 s each unless noted. "Hang" means a KGSL hang and `VK_ERROR_DEVICE_LOST`.

| Switched off | Result |
| --- | --- |
| everything above | no hang |
| dualsrc, bbox, ssaa, depthclamp, largepoints | no hang |
| primrestart, revdepth, dynidx, palette, lodbias | hang (2,804 errors) |
| dualsrc, bbox | no hang |
| ssaa, depthclamp, largepoints | hang |
| **dualsrc** | **no hang** (also over 120 s) |
| bbox | hang |
| nothing (120 s) | hang at 11 s, 9,197 errors |
| nothing, but dual-source blending marked broken (`DOLPHIN_UT_VK_BUG_DSB=1`, 90 s) | **no hang** |

**Dual-source blending triggers the hang** in Qualcomm's V@530 Vulkan driver. Dolphin already knows this kind of driver bug: `BUG_BROKEN_DUAL_SOURCE_BLENDING` is set for some AMD, Intel and Apple drivers.
- With the bug marked, Dolphin compiles separate shaders and uses dual-source blending only for draws whose blending needs it (`PipelineUtils.cpp`, `ShaderCache.cpp`, `UberShaderPixel.cpp`). Rendering stays exact.
- With the feature off, draws that need it are approximated.

The marked-bug run did not hang at the title screen. Whether Wind Waker needs dual-source blending somewhere later in the game is open: the play test below.

**Changing this is your decision and your change** (`roadmap.md`, "Rules for this fork": it changes the shaders Dolphin generates). The candidate is one line in `Source/Core/VideoCommon/DriverDetails.cpp`, `m_known_bugs`:

```
{API_VULKAN, OS_ALL, VENDOR_QUALCOMM, DRIVER_QUALCOMM, Family::UNKNOWN,
 BUG_BROKEN_DUAL_SOURCE_BLENDING, -1.0, <end version>, true},
```

Open questions for that line:
- **The end version.** Only V@530 is known to hang. The Android app on newer drivers runs the game, so an end version would keep newer drivers exact.
- **`OS_ALL` or `OS_LINUX`.** The same blob on Android likely hangs too.
- **Turnip.** Dolphin identifies Turnip as `DRIVER_QUALCOMM` as well (the device name contains "Adreno"), with a Mesa version number. Turnip 26.0.0 reports `0x06800000`, below `0x80212000`, so a range from -1 would include it. Turnip does not need the entry (step 6).

### The app no longer sits on a lost GPU (frontend, done)

`Emulator`, building on 3.8's fallback:
- The alert handler passes alerts to `Emulator::NoteAlert`. At the first `VK_ERROR_DEVICE_LOST`, the game is stopped, and the repeats are counted instead of shown.
- When the core is idle again, the game starts again on OpenGL ES for this run, with a note on screen: "The GPU driver stopped responding on Vulkan: this game restarted on OpenGL ES. Game settings can choose it for good."
- Progress since the start is lost, but it was lost anyway: nothing was being drawn.
- Checked: Wind Waker on Vulkan stopped at 11 s with **1 alert logged** (4 more counted, not shown), then ran on OpenGL ES 3 s later at 100 % speed.

## Step 6: custom Vulkan drivers (spike): **yes**

The goal: the Android app's "GPU driver" option (libadrenotools), for example Mesa Turnip from K11MCH1/AdrenoToolsDrivers. The spike tried these routes, in order:

| Route | Result |
| --- | --- |
| `HYBRIS_LD_LIBRARY_PATH` holding a folder with `vulkan.adreno.so` | No: Android's loader loads the driver through libhybris' isolated "sphal" namespace (`android_load_sphal_library`), which searches only the vendor partition |
| Android's own "updatable driver": `GraphicsEnv::setDriverPathAndSphalLibraries`, called through `android_dlsym` (with a hand-made libc++ `std::string`) | The path is accepted, but `getDriverNamespace()` returns null: it needs an exported "vndk" namespace, which libhybris does not have (`android_get_exported_namespace("vndk")` = null; "sphal" and "default" exist). Android's `UPDATABLE_GFX_DRIVER` variable asks a GPU service over Binder instead, which is not there |
| `HYBRIS_USE_VENDOR_NAMESPACE=1` | Breaks Qt's EGL ("failed to register buffer") |
| Preloading the driver into "sphal" by path | Refused: "sphal" permits only the vendor partitions and `/data` (`/android/data`, not writable by phablet) |
| A bionic shim exporting `android_load_sphal_library`, via `HYBRIS_LD_PRELOAD` | Ignored: bionic applies preloads only when it links an Android executable, and libhybris has none |
| The same shim, `android_dlopen`ed with `RTLD_GLOBAL` | Loaded (from a folder in `HYBRIS_LD_LIBRARY_PATH`: libhybris' default namespace is isolated too, but accepts its own search path), but Android's loader still bound the real function |
| **A stand-in `libvndksupport.so`**, first in `HYBRIS_LD_LIBRARY_PATH` | **Works.** Android's loader gets it as its `libvndksupport` dependency. Its `android_load_sphal_library` loads `<folder>/vulkan.adreno.so` from a namespace of its own, and does what Android's does for everything else |

The working route:
- **`ut/vkdriver/vkdriver-shim.c`** (and `vkdriver-shim.map`):
  - Android's `libvndksupport` re-implemented: `android_load_sphal_library`, `android_unload_sphal_library`, `android_is_in_vendor_process`, with its `LIBVNDKSUPPORT` symbol versions. It picks the namespace like Android's (sphal, then vendor, then default).
  - With `DOLPHIN_UT_VK_DRIVER_DIR` set, `vulkan.*` comes from `<dir>/vulkan.adreno.so`, loaded in a "shared" namespace of its own. That namespace starts as a copy of the default one, so the driver's dependencies (libc, liblog, libcutils, libhardware, libnativewindow, libsync, libz) are found.
  - It does not call `android_link_namespaces`: libhybris does not implement it, and the call traps (SIGTRAP in `ld-android.so`, found from a core dump).
- **`ut/vkdriver/build-shim.sh`** builds it without the Android NDK. It uses the cross container's arm64 GCC with `-nostdlib`, linked against the phone's own bionic `libc.so`, `libdl.so` and `libdl_android.so` (pulled over adb into `build-ut/vkdriver/bionic/`). The result is 7.7 KB, and imports only bionic functions.
- **`Source/Core/DolphinUT/VulkanDriver.{h,cpp}`**: `VulkanDriver::Use(folder, shim_folder)` sets `HYBRIS_LD_LIBRARY_PATH` and `DOLPHIN_UT_VK_DRIVER_DIR` at the start of `main`. libhybris starts later, with the first Android library (Qt's EGL), so the app can choose the driver itself, with no launcher change. Android's loader loads its driver once per process, so a change applies at the next start, as in the Android app.
  - For now `DOLPHIN_UT_GPU_DRIVER=<folder>` selects it; the shim is expected in `<binary folder>/lib/hybris/`. On the phone:
    - `/home/phablet/dolphin-ut-arm64/lib/hybris/libvndksupport.so`;
    - Turnip in `/home/phablet/dolphin-ut-turnip/driver/vulkan.adreno.so`.

Checked with **Mesa Turnip v26.0.0 R8** (from K11MCH1/AdrenoToolsDrivers `v26.0.0-rc08`, `Turnip_v26.0.0_R8.zip`; MIT licensed; it lists `FD619`). The file was renamed only; its soname needs no patch.
- The log shows `[vkdriver-shim] …/vulkan.adreno.so for vulkan.adreno.so: loaded`, and Dolphin sees **162 Vulkan extensions** (the vendor driver: 76), including `VK_EXT_extended_dynamic_state3` and `VK_KHR_maintenance5`.
- **Wind Waker on Turnip: no hang** (90 s and 60 s from boot, 100 % speed), with dual-source blending on and no driver-bug entry.
- Need for Speed on Turnip: 100 % speed.
- Presenting through libhybris' Wayland window works (the frames are counted); whether the picture looks right needs the screen (manual test).

What the feature needs, as the Android app has it (to build after your on-screen tests):
- a "GPU driver" setting: the system driver (default) or an installed one;
- "Install driver" from a zip picked with the file picker: read `meta.json` (`libraryName`, name, version), and unpack the library as `vulkan.adreno.so` into `~/.local/share/<package>/GpuDrivers/<name>/`;
- a note that a change applies at the next start;
- the shim built and shipped in the click (`lib/hybris/libvndksupport.so`), with the bionic link libraries made reproducible (generated stubs instead of copies pulled from the phone).

The app would not bundle or download drivers. Some in that repository are Qualcomm's own (`Qualcomm_*_adpkg.zip`), which cannot be redistributed.

## Step 3: compatibility table

Each game from its save state in slot 1 (your states in `dolphin-ut-user-w`), 60 s, dual core, CPU boost 1024. `ut/phone-debug/bench.sh` gives the mean speed from 10 s on; clocks are means over the run. The phone was locked (the picture still renders). `bench.sh` now looks only for the copy it started: your click, stopped in the background, made it wait forever the first time.

| Game (state) | System Vulkan (V@530) | OpenGL ES | Turnip v26.0.0 R8 |
| --- | --- | --- | --- |
| Need for Speed: Most Wanted (race) | 91 %, 30 fps; GPU 665 MHz, 63 % busy | **98 %**, 35 fps; 665 MHz, 62 % | 94 %, 31 fps; 665 MHz, 66 % |
| Wind Waker (in game, not the title) | 100 %, 30 fps; no hang in 60 s | 100 %, 30 fps | 100 %, 30 fps |
| Newer SMB (in a level) | **100 %**, 60 fps | 96 %, 57.5 fps | **100 %**, 60 fps |

Notes:
- **Every game is at or near full speed on every driver** with dual core and the boost. The differences are small: OpenGL ES leads in Need for Speed (as in 3.0), Vulkan and Turnip in Newer SMB.
- **Wind Waker did not hang on the system driver from its in-game state.** So the hang needs something drawn at the title screen (11 s from boot, every time), not everything with dual-source blending. The game might still hit it elsewhere: S4-M2.
- Newer SMB showed one "GFX FIFO: Unknown Opcode" alert right after its state loaded, on all three drivers alike. It is not a driver problem. It is a known Dolphin symptom of dual core (or of a state saved before dual core became the default), about emulated behavior, so it is noted, not changed. The game kept running at full speed.
- The phone was charging and idle otherwise. A long run (step 4) shows what happens when it heats up.

## Step 7: Qualcomm entries that Dolphin applies only on Android (risk 6)

This phone runs the same kind of Qualcomm blob as Android, but Dolphin reports `OS_LINUX` here, so these entries in `DriverDetails.cpp` stay off:

| Entry | What it does | On this phone |
| --- | --- | --- |
| `BUG_SLOW_OPTIMAL_IMAGE_TO_BUFFER_COPY` (Vulkan) | Avoids `vkCmdCopyImageToBuffer` from optimally tiled images, which allocates a staging image on Adreno. Speed only | Likely the same blob behavior. No slowdown seen yet that points at it (EFB copies to RAM, Wind Waker's Picto Box). A speed workaround: no pixel change |
| `BUG_BROKEN_TEV_COLOR_ZERO` (GL ≥ 700, Vulkan ≥ `0x802bc000`) | Feeds 0 through the konst table, where some drivers miscompile specialized shaders | **Does not apply:** this driver is V@530 (`0x80212000`), below both thresholds |
| `BUG_BROKEN_MULTITHREADED_SHADER_PRECOMPILATION` (all Android drivers) | Compiles shaders on one thread at start, instead of several | No crash seen with several threads (shader compilation at "Synchronous" by default). Not needed so far |

The new finding is **not** in that list: dual-source blending (step 1) is broken on this driver and is not marked on Android either. All four are your decision (`roadmap.md`, rules).

## Steps 4 and 5: on screen (manual tests)

- **Step 4 (a long run as an installed app)** needs the screen on and unlocked: Lomiri stops a managed app behind the lock screen. `ut/phone-debug/endurance.sh` samples every 30 s, in the background so the phone can be unplugged:
  - skin, big-core, GPU and battery temperatures;
  - charge and current;
  - big-core and GPU clocks, and GPU load.

  At the end it adds the click's `perf:` lines from the journal. Results go to `/home/phablet/dolphin-ut-endurance.txt`.

  The first version died at the unplug (S4-M4 results). Unplugging makes systemd stop `adbd.service`, which kills every process in its cgroup, `nohup` or not. The script now restarts itself as a unit of the user's systemd (`systemd-run --user`, unit `dolphin-ut-endurance`), so it is started with a plain `adb shell sh …/endurance.sh 900`.
- **Step 5 (frame pacing)** uses stone-ut's `fps-panel.py` (pushed to `/home/phablet/dolphin-ut-probe/`), which needs the screen on. Switching the panel to 60 Hz needs `sudo stone-refresh-rate 60` and a display restart.

## Decisions for the user

| Decision | Options | Recommendation |
| --- | --- | --- |
| Wind Waker on the system driver (step 1) | (a) a `BUG_BROKEN_DUAL_SOURCE_BLENDING` line for this driver in `DriverDetails.cpp`, written by you; (b) nothing in Dolphin, and Turnip or OpenGL ES for such games; (c) a port default that starts known problem games on OpenGL ES (roadmap step 2) | **(a)**: S4-M2 passed (no hang, nothing broken). Scope it so Turnip stays out: see "Results of the manual tests" |
| Custom GPU drivers (step 6) | build the feature (setting, install from zip, shim in the click) or leave it at the environment variable | Build it: the picture is right, and the crash when a game stopped is found and fixed (S4-M1 results). The system driver stays the default |
| The other Android-only Qualcomm entries (step 7) | enable any of them on Linux | None for now: none applies or is needed on V@530 |

## Manual tests (results below, "Results of the manual tests")

Screen on, unlocked, from an interactive `adb shell`. These use the `run-dolphin.sh` test build (your test folder `dolphin-ut-user-w`), which has the diagnostic switches and the driver shim. For a game other than the default, set `GAME=`.

**S4-M1. Turnip, on screen.**

```
DOLPHIN_UT_GPU_DRIVER=/home/phablet/dolphin-ut-turnip/driver GAME=/home/phablet/Documents/Dolphin/Games/windwaker.iso sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut-start 900 --backend Vulkan
```

- Play Wind Waker for 10 minutes or more, past the title: does the picture look right (sea, sky, sun, characters), and does it play smoothly? Any hang?
- The same with Need for Speed and Newer SMB (`GAME=` their paths in `/home/phablet/dolphin-ut-games/`): do they look and run as on the system driver?

Everything renders as expected with the Turnip driver. 

**S4-M2. The system driver with dual-source blending marked broken** (option (a) above, without changing Dolphin):

```
DOLPHIN_UT_VK_BUG_DSB=1 GAME=/home/phablet/Documents/Dolphin/Games/windwaker.iso sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut-start 900 --backend Vulkan
```

Play 10 minutes past the title: any hang, or anything that looks different from OpenGL ES (water, fog, the sun's glow, transparencies)?

No hang, or anything broken.

**S4-M3. A lost GPU, on screen.** The same command without the variable (`GAME=…windwaker.iso sh run-dolphin.sh ut-start 120 --backend Vulkan`). About 11 s in, does the game restart by itself on OpenGL ES, with the yellow note?

Yes, the game does exactly that. 

**S4-M4. A long run as an installed app** (step 4).
- Unplug the phone, then start the click **Dolphin** from the app drawer and a game (Need for Speed: one race after another, ideally).
- Before unplugging, start the sampler: `adb shell 'nohup sh /home/phablet/dolphin-ut-probe/endurance.sh 900 >/dev/null 2>&1 &'`.
- Play 15 minutes, then plug in again. Results: `/home/phablet/dolphin-ut-endurance.txt` (I can read it).
- Does the phone get uncomfortably hot, and does the game slow down towards the end?

The phone never barely got warm after all 4 tests. Please collect the log.

**S4-M5 (optional). Frame pacing** (step 5). During a game: `python3 /home/phablet/dolphin-ut-probe/fps-panel.py 30` in another `adb shell`. It prints the panel's real refresh, the frames committed per second, and GPU load. To compare at 60 Hz: `sudo stone-refresh-rate 60 --restart-display`, then the same again, then `sudo stone-refresh-rate 120 --restart-display`.


phablet@ubuntu-phablet:~$ DOLPHIN_UT_GPU_DRIVER=/home/phablet/dolphin-ut-turnip/driver GAME=/home/phablet/Documents/Dolphin/Games/windwaker.iso sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut-start 900 --backend Vulkan
running: ut-start: -u /home/phablet/dolphin-ut-user-w --start /home/phablet/Documents/Dolphin/Games/windwaker.iso --backend Vulkan (for 900s, log /home/phablet/dolphin-ut-user-w/run-ut-start.log)
stopped after 900s
14:30:440 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
14:31:491 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
23:19:550 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/StateSaves/GZLP01.s09.dtm does not exist
28:12:225 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/StateSaves/GZLP01.s09.dtm does not exist
phablet@ubuntu-phablet:~$ cd Documents
phablet@ubuntu-phablet:~/Documents$ cd Dolphin
phablet@ubuntu-phablet:~/Documents/Dolphin$ ls
Games
phablet@ubuntu-phablet:~/Documents/Dolphin$ cd Games
phablet@ubuntu-phablet:~/Documents/Dolphin/Games$ ls
'Need for Speed - Most Wanted.iso'  'Newer Super Mario Bros. 1.3.0 [SMNW03].wbfs'   windwaker.iso
phablet@ubuntu-phablet:~/Documents/Dolphin/Games$ cd ../../
phablet@ubuntu-phablet:~/Documents$ cd ..
phablet@ubuntu-phablet:~$ DOLPHIN_UT_GPU_DRIVER=/home/phablet/dolphin-ut-turnip/driver GAME=/home/phablet/Documents/Dolphin/Games/Need for Speed - Most Wanted.iso sh /home/phablet/dolphi
n-ut-probe/run-dolphin.sh ut-start 900 --backend Vulkan
-bash: for: command not found
phablet@ubuntu-phablet:~$ DOLPHIN_UT_GPU_DRIVER=/home/phablet/dolphin-ut-turnip/driver GAME='/home/phablet/Documents/Dolphin/Games/Need for Speed - Most Wanted.iso' sh /home/phablet/dolp
hin-ut-probe/run-dolphin.sh ut-start 900 --backend Vulkan
running: ut-start: -u /home/phablet/dolphin-ut-user-w --start /home/phablet/Documents/Dolphin/Games/Need for Speed - Most Wanted.iso --backend Vulkan (for 900s, log /home/phablet/dolphin-ut-user-w/run-ut-start.log)
exited by itself after 505s (status 135)
crashed (signal 7); crash data:
no register dump in the log
  core dump: -rw------- 1 phablet phablet 70028767232 Nov 16 13:40 core
31:47:955 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
31:49:387 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
phablet@ubuntu-phablet:~$ DOLPHIN_UT_VK_BUG_DSB=1 GAME=/home/phablet/Documents/Dolphin/Games/windwaker.iso sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut-start 900 --backend Vulkan
running: ut-start: -u /home/phablet/dolphin-ut-user-w --start /home/phablet/Documents/Dolphin/Games/windwaker.iso --backend Vulkan (for 900s, log /home/phablet/dolphin-ut-user-w/run-ut-start.log)
^C
phablet@ubuntu-phablet:~$ GAME=/home/phablet/Documents/Dolphin/Games/windwaker.iso sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut-start 900 --backend Vulkan
running: ut-start: -u /home/phablet/dolphin-ut-user-w --start /home/phablet/Documents/Dolphin/Games/windwaker.iso --backend Vulkan (for 900s, log /home/phablet/dolphin-ut-user-w/run-ut-start.log)
^C
phablet@ubuntu-phablet:~$ adb shell 'nohup sh /home/phablet/dolphin-ut-probe/endurance.sh 900 >/dev/null 2>&1 &'
* daemon not running; starting now at tcp:5037
* daemon started successfully
adb: no devices/emulators found
phablet@ubuntu-phablet:~$ exit
logout
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell 'nohup sh /home/phablet/dolphin-ut-probe/endurance.sh 900 >/dev/null 2>&1 &'
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ 

## Results of the manual tests

| Test | Result |
| --- | --- |
| S4-M1 Turnip on screen | **Picture: pass.** Wind Waker 15 min, no hang; Need for Speed renders right. Need for Speed **crashed when the game was stopped** (SIGBUS): every Turnip game did. **Found and fixed**, below. Newer SMB was not played on screen (only the 60 s matrix run) |
| S4-M2 dual-source blending marked broken | **Pass:** no hang, nothing broken |
| S4-M3 lost GPU on screen | **Pass:** restarts on OpenGL ES by itself, with the note |
| S4-M4 long run, unplugged | **Pass, on OpenGL ES.** Phone barely warm. Speed held, but the temperature samples were lost (below) |

### S4-M1: Turnip crashed when a game stopped (SIGBUS): found and fixed

`exited by itself after 505s (status 135)`: signal 7, SIGBUS. It happened when you left the game, not during play.

**Reproduced** on the first try: Need for Speed on Turnip from save state 1, with the app's own stop after 20 s (`--stop-after 20000`, the same path as the Stop button). The process died a few milliseconds into the shutdown, at "Video Loop Ended".

**Cause: a pthread function libhybris does not provide.**
- `ut/phone-debug/core-info.py` reads the kept core on the phone, using the core's own list of mapped files. The crashing thread:
  - Dolphin → Android's `libvulkan.so` (`vkDestroyDevice`) → Turnip's `tu_DestroyDevice` → Mesa's `util_queue_finish` → `pthread_barrier_init`;
  - which jumped to `0xfffffffffffffffe`: `BUS_ADRALN`, misaligned PC.
- libhybris routes an Android library's pthread functions to glibc through its hook table, because bionic's and glibc's pthread types differ. For a `pthread*` name missing from the table it returns a numbered fake address (-2, -3, …), so that a call crashes recognizably.
- Its table (libhybris on the phone, `0.1.0+git20260624+7079712`) has no `pthread_barrier_*`. The system driver never calls them; Turnip does, in every `vkDestroyDevice`. **So every Turnip game crashed when it stopped.**
- The runs ended by `run-dolphin.sh` (the compatibility matrix, the 12-minute replay, your Wind Waker run) most likely crashed on the way out too, unseen: the script did not report the exit status after its own SIGINT. **Fixed:** it now reports a crash during shutdown.

**Fix (`DolphinUT/VulkanDriver.cpp`, port code, not emulation):**
- libhybris asks an application's callback first (`hybris_set_hook_callback`, exported by `libhybris-common.so.1`). Its lookup calls `callback(symbol, requester)` and uses any non-null result; checked in the phone's binary.
- With a custom driver, `VulkanDriver::Use()` registers a callback that returns glibc's `pthread_barrier_init/wait/destroy`.
- The types match on arm64: 32 bytes on both, both return -1 to the serial thread, and Mesa passes no attributes. Without a custom driver nothing is registered.

**Checked after the fix:**

| Run | Before | After |
| --- | --- | --- |
| Need for Speed, Turnip, stop after 20 s | SIGBUS at stop | back to the library ("core: idle"), clean exit |
| Wind Waker, Turnip, stop after 15 s | | back to the library, clean exit |
| Newer SMB, Turnip, stop after 15 s | | back to the library, clean exit (the known "Unknown Opcode" alert, as on every driver) |
| Need for Speed, system driver | clean | clean (no hook registered) |
| Need for Speed, Turnip, ended by the script | (unreported) | clean exit |

**Other findings on the way:**
- The first crash's data was lost. The core (in `dolphin-ut-arm64/`) and the log were deleted by your next run (`rm -f core` at the start of `run-dolphin.sh`). **Fixed:** after a crash the script moves them to `dolphin-ut-user-w/crash-<date>-<mode>/`.
  - The memory-map snapshot of the crash was empty: the 2 s snapshot loop copied `/proc/PID/maps` once more after the process died. **Fixed:** it keeps the last non-empty copy.
  - The 70 GB core is sparse, mostly Dolphin's reserved fastmem address space.
- **Memory is flat over a long Turnip run.** Need for Speed from save state 1, 720 s with the screen locked, 97% mean speed. `ut/phone-debug/mem-watch.sh` sampled every 10 s:
  - resident 853 → 880 MB;
  - GPU memory (KGSL) a constant 294 MB;
  - free memory ~4.5 GB, swap unused.
- **Query timeouts on Turnip:** 6 `vkGetQueryPoolResults failed: VK_TIMEOUT` in those 720 s, against 0 in every system-driver run.
  - Dolphin reads the game's GPU queries (`VKPerfQuery.cpp`) only after the GPU has finished that work, waiting with `VK_QUERY_RESULT_WAIT_BIT`. Turnip gave up after its own 2 s wait.
  - Speed stayed at 100% around each one, so this is noted only.
- **Other unhooked functions:** Turnip may import other pthread functions that libhybris lacks. Only the barrier ones were hit in these tests. A future one would crash the same way (PC `0xffff…fX`), and `core-info.py` names it.

### S4-M4: the long run

- **The temperature sampler died at the unplug.** It took one sample at 13:50:53 and the cable came out at 13:50:56: systemd stopped `adbd.service` and killed everything in its cgroup. **Fixed:** `endurance.sh` now runs as a user systemd unit (steps 4 and 5 above). Tested: it runs in `user@32011.service/app.slice`, outside adbd.
- **It ran on OpenGL ES, not Vulkan.** Need for Speed has a per-game backend, `OGL`, set in the click at 13:51:23, just before the first start ("setting StringSetting.MAIN_GFX_BACKEND = OGL (game GOWP69)"). So this was an OpenGL ES endurance run, which is what that game uses on this phone (it was the fastest of the three in step 3).
- **Speed, from the click's journal** (`perf:` every 5 s, 13:51–14:09, about 18 minutes of play in 4 starts, using save slot 9):

  | Minutes | Mean speed | Lowest 5 s |
  | --- | --- | --- |
  | 13:51–13:55 | 99–101% | 85% (first seconds after boot) |
  | 13:56–14:00 | 98–101% | 92% |
  | 14:02–14:05 | 99% | 89% |
  | 14:06–14:09 | 97–100% | 86% |

  No slowdown towards the end, so no thermal throttling that the game shows.
- **Battery** (upower history): 100% at the unplug, 99% at 14:02:58, 98% at 14:06:38: about 2% in 16 minutes of play, around 7–8% an hour.
- **Temperature:** only your report: the phone barely got warm.

### What this means for the decisions

- **`DriverDetails` line (option (a)):** S4-M2 passed, so (a) is the recommendation. For the scope:
  - Qualcomm's blob reports `0x80000000 | (V@ number << 12)`, so V@530 is `0x80212000`. Turnip reports a Mesa version (26.0.0 is `0x06800000`).
  - A range from `0x80000000` (inclusive) to `0x80213000` (exclusive) covers the blob up to V@530 and leaves Turnip and newer blobs out:

    ```
    {API_VULKAN, OS_ALL, VENDOR_QUALCOMM, DRIVER_QUALCOMM, Family::UNKNOWN,
     BUG_BROKEN_DUAL_SOURCE_BLENDING, 2147483648.0, 2149658624.0, true},
    ```

    `2147483648.0` is `0x80000000`; `2149658624.0` is `0x80213000`.
  - Older V@5xx blobs fall inside; whether they hang is not known.
  - `OS_ALL` against `OS_LINUX` stays your call.
- **Driver feature:** the picture is right on all three games, Wind Waker no longer hangs, and the crash at stop is fixed. Build it, keeping the system driver as the default. Label a downloaded driver as "custom", since every new Mesa release can import something libhybris lacks.

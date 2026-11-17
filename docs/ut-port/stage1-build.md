# Stage 1: build environment and a CPU-only run

> The current build instructions are in [UbuntuTouchSetup.md](../../UbuntuTouchSetup.md). This document records how the build environment came about.

Stage 1 of `roadmap.md`. Goal: an arm64 Dolphin built against the phone's libraries, running the emulation core on the phone without a window, plus the Qt 6 Widgets check left over from stage 0.

Started 2026-11-14:
- source: upstream master `f9cb5f1c08` (Dolphin `2609-234`);
- phone: as in `stage0-probe.md`;
- build PC: 56 threads, 188 GB RAM, Docker 29.4.1.

## Build environment

Decision: build in **our own cross-build container** (amd64 host, arm64 target), not in clickable's image and not in an emulated arm64 container.
- clickable's `amd64-ut24.04-1.x-arm64` image follows the **24.04-1.x** channel. The phone runs **24.04-2.x** (Qt 6.10.2), and the binaries must link against the phone's Qt.
- An arm64 container under qemu would match "arm64 container" literally, but it would compile several times slower.
- The cross container below installs the phone's own arm64 packages through multiarch, so the result is the same.
- clickable can use a custom image later (stage 5).

Files:
- `ut/docker/Dockerfile`, image `dolphin-ut-cross`:
  - Ubuntu 24.04 amd64, `dpkg --add-architecture arm64`;
  - amd64 from `archive.ubuntu.com`, arm64 from `ports.ubuntu.com` (noble, -updates, -security);
  - `deb http://repo.ubports.com/ 24.04-2.x main` with the phone's key (`ut/docker/ubports.gpg`, pulled from the phone's `/etc/apt/trusted.gpg.d/`; "UBports Apt Signing Key (2026)", `A8DF67CB20262175118B863720049BF75A816E99`), pinned at 2000 as on the phone.
- `ut/docker/aarch64-linux-gnu.cmake`: the CMake toolchain:
  - `aarch64-linux-gnu-gcc/g++`;
  - multiarch library paths;
  - `QT_HOST_PATH=/usr`, so moc, rcc and uic come from the amd64 Qt of the same version.
- `ut/build.sh`: configures (once) and builds into `build-ut/dolphin` inside the container. `--package` makes `build-ut/dolphin-ut-arm64.tar.gz` (binaries, `Sys/`, `Languages/`); `--reconfigure` and `--image` do what they say. `build-ut/` is already covered by `.gitignore`.
- `ut/phone-debug/ut-env.sh`: sourced in an `adb shell` to get a Lomiri app's environment (see "libhybris static TLS").

Versions in the image (all match the phone):

| Item | Version |
| --- | --- |
| Cross compiler | `aarch64-linux-gnu-g++` 13.3.0 (Dolphin needs GCC 12 or newer) |
| CMake | 3.28.3 (Dolphin needs 3.25 or newer) |
| Qt 6 (amd64 tools and arm64 libraries) | `6.10.2+dfsg-14+0~20260608121858.24+ubports24.04.2~1.gbpe1e823`, the phone's build |
| PulseAudio | `1:16.1+dfsg1-2ubuntu10.1ubports1+…`, UBports' build as on the phone |
| glibc | 2.39 (noble), as on the phone |

One snag: `libglib2.0-dev:arm64` (pulled in by `qt6-base-private-dev` and `libpulse-dev`) depends on `python3 | qemu-user | qemu-user-static`. apt picks `python3:arm64`, whose postinst cannot run on amd64 (`Exec format error`). Installing amd64 `qemu-user-static` first satisfies the alternative.

Build the image: `docker build -t dolphin-ut-cross ut/docker` (`ut/build.sh` does it when the image is missing).

## Dolphin build

`ut/build.sh` configures with:

```
-DCMAKE_TOOLCHAIN_FILE=/opt/aarch64-linux-gnu.cmake -DCMAKE_BUILD_TYPE=Release
-DUSE_SYSTEM_LIBS=OFF
-DENABLE_QT=ON -DENABLE_NOGUI=ON -DENABLE_CLI_TOOL=ON
-DENABLE_TESTS=OFF -DENABLE_LLVM=OFF
-DENABLE_AUTOUPDATE=OFF -DENABLE_ANALYTICS=OFF -DUSE_DISCORD_PRESENCE=OFF
-DENCODE_FRAMEDUMPS=OFF
```

- `USE_SYSTEM_LIBS=OFF`: the phone lacks most optional system libraries (SFML, miniupnpc, mbedTLS, spng, fmt, SDL3, hidapi), so every optional dependency comes from `Externals/`. CMake reported "Using static … from Externals" for all of them.
- From the system:
  - Qt 6.10.2 ("Found Qt version 6.10.2");
  - X11/Xi/Xrandr ("X11 support enabled");
  - EGL ("EGL OpenGL interface enabled");
  - libevdev + libudev (evdev controller backend);
  - ALSA, PulseAudio (and Cubeb, bundled);
  - BlueZ ("enabling bluetooth support");
  - libsystemd (traversal server watchdog only);
  - Vulkan headers.
- X11 stays on until stage 2. The phone has libX11, libXi and libXrandr (Xwayland is installed).

Result, first build:
- **1948 targets in 2 min 40 s** (wall clock), no errors.
- `Binaries/`:
  - `dolphin-emu` 33.7 MB;
  - `dolphin-emu-nogui` 28.0 MB;
  - `dolphin-tool` 28.0 MB;
  - `traversal_server` 1.7 MB.
  - Release with symbols, not stripped.
- Package: `dolphin-ut-arm64.tar.gz`, 40 MB; 106 MB unpacked on the phone.

### Runtime dependencies

`NEEDED` across the three binaries:

```
ld-linux-aarch64.so.1 libEGL.so.1 libQt6Core.so.6 libQt6DBus.so.6 libQt6Gui.so.6 libQt6Widgets.so.6
libX11.so.6 libXi.so.6 libXrandr.so.2 libasound.so.2 libbluetooth.so.3 libc.so.6 libevdev.so.2
libgcc_s.so.1 libm.so.6 libpulse.so.0 libstdc++.so.6 libudev.so.1
```

All of them are present on the phone, so nothing has to be bundled for these binaries. With Wayland (stage 2), add `libwayland-client.so.0`, `libwayland-egl.so.1` and `libxkbcommon.so.0`, also present. Qt 6's xdg-shell plugin is the only addition that must be bundled (stage 0). Vulkan is `dlopen`ed (`libvulkan.so.1`, the hybris loader).

## On the phone

Deployed to `/home/phablet/dolphin-ut-arm64/` (`adb push` of the single tarball, then `tar xzf` on the phone; pushing a directory over adb drops the USB link). The user folder for tests is `/home/phablet/dolphin-ut-user` (`-u`). Dolphin uses the `Sys/` next to its binary when there is one (`CreateSysDirectoryPath()`).

| Check | Result |
| --- | --- |
| `dolphin-tool --help` | runs: `commands supported: [convert, verify, header, extract]` |
| `dolphin-emu-nogui --version` | `Dolphin 2609-234` |
| `dolphin-emu` (Qt UI), `DISPLAY=:0` (Xwayland, as upstream forces xcb), 15 s then SIGINT | starts: 19 threads, 91 MB RSS. Creates the whole user folder (`Config/Dolphin.ini`, `GFX.ini`, `GCPadNew.ini`, …). Stops cleanly on the signal ("A signal was received…"). Whether the window looked right: manual test S1-M3 |

### Unit tests on the phone

The emulation core cannot run without a game (manual test S1-M1). Dolphin's own unit tests exercise the host-side pieces the core depends on. They were built separately (`build-ut/dolphin-tests`, `-DENABLE_TESTS=ON -DENABLE_QT=OFF`, target `tests`, 1753 steps in 2 min 20 s) and run on the phone:

```
[==========] Running 1055 tests from 54 test suites.
[==========] 1055 tests from 54 test suites ran. (21765 ms total)
[  PASSED  ] 1055 tests.
```

This includes:
- `JitArm64` (10 tests: `MovI2R`, `ConvertSingleDouble`, `FPRF`, `Fres`, `Frsqrte` emitters, run as generated arm64 code);
- `PageFault` (the fault handler that fastmem uses; timing: start→HandleFault 14.9 µs, UnWriteProtectMemory 7.1 µs, HandleFault→end 10.9 µs, total 32.9 µs);
- `PageTableHostMappingTest` (16 tests).

So the arm64 JIT emitter, host page-fault handling and memory mappings work on this kernel (5.4, 4 KiB pages). The test binary stays on the phone as `/home/phablet/dolphin-ut-arm64/unittests` (log: `unittests.log`).

### CPU cores

`nproc` printed 6 or 7 in different runs, although all 8 cores are online (`/sys/devices/system/cpu/online` = `0-7`).
- Cores: six Cortex-A55 (`0xd05`, up to 1.80 GHz) and two Cortex-A78 (`0xd41`, up to 2.02 GHz).
- The cause is Qualcomm `core_ctl`. On the little cluster it is enabled with min 4 and max 6 active cores, and it isolates idle little cores (CPU4 and CPU5 were isolated). On the big cluster (CPU6–7) it is disabled, so both A78s are always available.
- Session processes are not restricted: `Cpus_allowed_list` is `0-7` and the cpuset is `/user.slice`.
- This does not hold Dolphin back. Whether its CPU and GPU threads land on the A78s is a stage 4 question.

## libhybris static TLS

A native Qt 6 program started from `adb shell` failed to get a GPU:

```
ERROR: Failed to load hybris linker for Android SDK version 29: /usr/lib/aarch64-linux-gnu/libhybris/linker/q.so: cannot allocate memory in static TLS block
```

Lomiri's session, and therefore every app it launches, has:

```
GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096
LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libtls-padding.so
```

These come from `/etc/profile.d/hybris-static-tls.sh` and the session environment. A plain `adb shell` has neither. `ut/phone-debug/ut-env.sh` sets them, plus `XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`, `DISPLAY`, `DBUS_SESSION_BUS_ADDRESS`, `QT_WAYLAND_DISABLE_WINDOWDECORATION=1` and `QT_IM_MODULE=maliit`. Its `ut_qt6_wayland` function adds `QT_QPA_PLATFORM=wayland` and the unpacked xdg-shell plugin. A click app gets the TLS settings from Lomiri. Any test or launcher outside the session must set them, and Dolphin's own launcher should not clear them.

## Qt 6 Widgets on Lomiri's Wayland (roadmap stage 1 step 5)

Test program: `ut/qt6-probe/` (`main.cpp`, `CMakeLists.txt`), built in the container (`build-ut/qt6-probe/qt6-probe`). It shows:
- a Widgets main window: label, button, line edit;
- a `QOpenGLWindow` in a window container. The phone has no `libQt6OpenGLWidgets`, so there is no `QOpenGLWidget`;
- an orange native child widget (`WA_NativeWindow`), like DolphinQt's render widget with "Render to Main Window";
- after 4 s, a blue second top-level widget, shown fullscreen for 6 s, like Dolphin's render window.

It logs every step, the `wl_display`/`wl_surface` of each window (read through `QPlatformNativeInterface::nativeResourceForWindow()`, as DolphinQt's `GetWindowSystemInfo()` does), touch and mouse events, and the GL frame rate. Options: `--seconds N`, `--no-second-window`, `--no-gl`, `--no-native-child`.

Run (screen off, automated):

```sh
. /home/phablet/dolphin-ut-probe/ut-env.sh; ut_qt6_wayland
cd /home/phablet/dolphin-ut-probe && ./qt6-probe --seconds 14
```

```
qt.qpa.wayland: Using the 'xdg-shell' shell integration
qt.qpa.wayland: using input method: MInputContext
Qt 6.10.2, platform 'wayland'
screen 'Screen6' 1080x2400 available 1080x2400 dpr=1.00 logical dpi=96.0 refresh=120.00
qt.qpa.wayland: Using Wayland-EGL
QOpenGLWindow: GL_RENDERER=Adreno (TM) 619 GL_VERSION=OpenGL ES 3.2 V@0530.0 ...
xdg_toplevel.configure 1080x2305 WindowActive
main window:  wl_display=0x5593207ce0 wl_surface=0x5593273580 size=1080x2305 dpr=1.00
native child: wl_display=0x5593207ce0 wl_surface=0x559326de40 size=1058x200
GL window:    wl_display=0x5593207ce0 wl_surface=0x55933d3b10 size=1058x1889
second top-level window shown fullscreen
xdg_toplevel.configure 1080x2400 WindowFullScreen | WindowActive   (then 1080x2305 WindowFullScreen|WindowActive)
render window: wl_surface=0x5593542f80 size=1080x2305
closing the second window
exit 0
```

What it shows:
- **Qt 6 Widgets runs natively on Lomiri's Wayland**, with the bundled xdg-shell plugin. Platform `wayland`, maliit input method, xdg_wm_base v1 toplevels.
- **DolphinQt's way of getting the native handles works**: `display` and `surface` return a real `wl_display` and `wl_surface` for top-level windows and for a native child. That is what #14962 passes to EGL and Vulkan.
- A second top-level window goes fullscreen (1080x2400) and closes cleanly. Lomiri then sent another configure of 1080x2305 while still fullscreen, so the window ended up at 1080x2305. The same thing happened in stage 0 with the screen off. With the screen on (S1-M2), the fullscreen window kept 1080x2400 and covered the top panel.
- **Qt's own GL path fails to make a window surface:** `Cannot find EGLConfig, returning null config`, `Could not create EGL surface (EGL error 0x3005)`, `eglSwapBuffers failed with 0x300d`. Runs with `--no-gl` and `--no-native-child` isolate it: the errors appear **only with the `QOpenGLWindow` container**. Plain widgets, the native child and the second window all draw through `wl_shm` (raster) with no errors. The GL context itself is created (GLES 3.2), but not a window surface from Qt's first EGL config choice. On screen (S1-M2) the GL area still animated, so Qt recovers. This build's Qt is probably asking for a desktop-GL-capable config, while libhybris offers only `EGL_OPENGL_ES*_BIT`.
  - **DolphinQt does not use Qt's GL.** Its render widget is a native `QWidget`, and Dolphin's own `GLContextEGLWayland` / Vulkan draw into its `wl_surface`. So this does not block the port.
  - It would matter if a Qt Quick or `QOpenGLWidget` frontend were added later (roadmap stage 6). Something like `QSurfaceFormat::setDefaultFormat()` with `RenderableType::OpenGLES` would need trying then.

## Manual tests (2026-11-14)

Stage 1 is done: S1-M1 met the exit check. Only the wired controller is open: USB OTG works, but the second pad is a DualShock 4 clone that the 5.4 `hid-sony` driver rejects (it does not answer the MAC-address report `0x81`).

### S1-M1. A game, headless

The user copied a Wii game they own to the phone: `Newer Super Mario Bros. 1.3.0 [SMNW03].wbfs` (905,969,664 bytes) in `/home/phablet/dolphin-ut-games/`. The runs below were done over adb, with `. ut-env.sh` and a fresh user folder each time. The audio was live (Cubeb on PulseAudio), so the game could be heard from the phone's speaker.

**Run A: Null video backend, 60 s** (`dolphin-emu-nogui -p headless -v Null -u … -e …`). Logging was at INFO for BOOT, CORE, JIT, PowerPC, DVD, IOS*, DSPHLE, Audio, OSREPORT and others, written to the console because `-u` does not move `Logs/` early enough.

```
Dolphin 2609-234 | JITARM64 SC | Null | HLE | SMNW03            (the title line, printed by the headless platform)
I[BOOT]: Starting core = Wii mode
I[Audio]: Cubeb initialized using pulse backend
W[JIT]: Dispatcher is 0x7ba67f1000
I[IOS]: Starting IOS 0000000100000050 ... IPC initialised.
N[BOOT]: Booting from disc: …/Newer Super Mario Bros. 1.3.0 [SMNW03].wbfs
I[BOOT]: Faking Wii BS2... Setup Wii Memory... Starting IOS 0000000100000035
Apploader Initialized.  (DVD reads of the apploader and main executable)
I[IOS_ES]: ES_DIVerify: Title context changed: 00010000534d4e57
N[OSREPORT]: Revolution OS / Kernel built : Feb 27 2009 / Memory 88 MB / << RVL_SDK - GX, VI, WPAD, KPAD, AI ... >>
I[DSPHLE]: CRC 4cc52064: Wii - AXWii chosen      (audio microcode running)
I[AI]: Audio DMA configured
```

- The game wrote its save: `Wii/title/00010000/534d4e57/data/NerSMBW.sav` and `banner.bin`. It also created the usual NAND entries (`setting.txt`, `SYSCONF`, `play_rec.dat`, `dvderror.dat`).
- No panic, no invalid memory access, no JIT exception in the log. The 65 lines that match "fault" are "Creating a default entry…".
- One warning: `Non-IEEE mode was requested, but host CPU is not known to support FEAT_AFP` (`Common/ArmFPURoundMode.cpp`). This is about the A55/A78 not advertising FEAT_AFP. It is an accuracy and speed detail of the arm64 JIT, the same on Android.
- Fastmem was on (the default; `Dolphin.ini` does not override it). With no fault reported, the page-fault path ran as in the `PageFault` unit test.

**Run B: OpenGL backend, headless, on the Adreno.**
- With no window, `GLContextEGL` opens the default EGL display. libhybris serves it, and Dolphin selects GLES itself:

  ```
  I[Video]: Error: couldn't get an EGL visual config      (the desktop-GL probe; harmless, it then falls back)
  I[Video]: Using OpenGL|ES
  I[Video]: Video Info: Qualcomm, Adreno (TM) 619, OpenGL ES 3.2 V@0530.0 (GIT@c263890aed, Ifb496a9c37, 1656315076) (Date:06/27/22)
  W[Video]: Missing OGL Extensions: PinnedMemory GSInstancing ClipControl DepthClamp
  ```

  The title line says `OpenGL ES`.
- To see the output, frames were dumped as images. The switches are `-C Dolphin.Movie.DumpFrames=True -C Graphics.Settings.DumpFramesAsImages=True`. Notes on getting there:
  - `-C` names the systems `Dolphin` and `Graphics`, not `Main` and `GFX`;
  - the switch that starts dumping is `Movie.DumpFrames`; `DumpFramesAsImages` only picks the format.
- 1111 PNG frames (836x456, the 16:9-corrected size) in about 40 s: about 28 per second, slowed by PNG encoding.
- The frames show the game's Wii Remote strap safety screen rendered correctly: frame 60 is the first panel, frame 600 the second panel with a different illustration, and frame 1110 is fading out to the next screen. The text is Traditional Chinese, the default Wii system language in the fresh user folder.

**Load, 10 s windows after 20 s, no frame dumping** (strap screen, so a light scene):

| Mode | Thread load | GPU |
| --- | --- | --- |
| Single core (`CPUThread=False`, upstream's non-Android default) | CPU-GPU thread 70 %, `threaded-ml` (PulseAudio) 16 % | 78 % busy at 700 MHz |
| Dual core (`CPUThread=True`, title `DC`) | CPU thread 57 %, Video thread 49 %, `threaded-ml` 18 % | 100 % busy at **266 MHz** |

No thread is pegged, so on this screen the game runs at full speed under Dolphin's speed limiter. This says nothing yet about real gameplay. Notes for stage 4:
- **Dual core is off by default on Linux** (`DEFAULT_CPU_THREAD` is `true` only `#if defined(ANDROID)`, "because the performance boost is really needed"). This phone needs the same. Set `CPUThread = True` in the port's default `Dolphin.ini` (a user setting, not a code change).
- **GPU clock:** the second run sat at 266 MHz while 100 % busy. The governor behaves as stone-ut saw for Lomiri (`roadmap-hardware-2.md` item 11), even with `mod_percent=200`.
- Two of the features Dolphin reported missing come from its own detection. `GSInstancing` is off because the driver lacks `GL_*_geometry_point_size`. `ClipControl` is off because only `GL_ARB_clip_control` is checked, not the `GL_EXT_clip_control` the driver has. Both are the same on Android with this driver. They affect rendering, so any change is the user's call.
- Thread-to-core placement could not be read: thread names with spaces shift the `/proc/…/stat` fields the quick script used.

**Result:** the exit check of stage 1 is met. A game boots on the phone, the arm64 JIT runs it with no faults, audio plays, saves are written, and it even renders through GLES on the Adreno headless.

### S1-M2. The Qt 6 probe on screen

Run by the user with the screen on. Reported:
- the main window showed the label, button and text field properly;
- the GL area was **animated teal**, not black;
- the orange native child was visible;
- the blue fullscreen window **covered the whole screen, including the top panel**;
- the on-screen keyboard appeared.

From the log (`--seconds 40`):

```
Qt 6.10.2, platform 'wayland'
Cannot find EGLConfig, returning null config               (as in the screen-off run)
qt.qpa.wayland: Could not create EGL surface (EGL error 0x3005)
QOpenGLWindow: GL_RENDERER=Adreno (TM) 619 GL_VERSION=OpenGL ES 3.2 …
main window: … size=1080x2305     native child: … size=1058x200     GL window: … size=1058x1889
touch begin on buttonWindow … mouse press on button … (source touchscreen) … button clicked (1) … (6)
second top-level window shown fullscreen … render window: … size=1080x2400
touch begin on editWindow … text: 'here'
touch begin on glcontainerWindow …   touch begin on nativechildWindow …
```

- **Touch reaches Qt 6 widgets**: touch events plus synthesized mouse presses from the `touchscreen` device. Six button clicks registered, taps on the GL container and the native child too.
- **The on-screen keyboard works** through maliit (`MInputContext`): typing gave `text: 'here'`.
- **Fullscreen gets the whole panel** (1080x2400) with the screen on. The 1080x2305 reconfigures happened only with the screen off.
- **Qt's GL window draws** (teal animation) despite the EGL config errors. Qt fails with its first config choice and then gets a working surface. Before a Qt Quick (QML) frontend is built, these errors should be cleared, probably by asking for an OpenGL ES default surface format. Qt Quick renders through this path.

### S1-M3. DolphinQt's UI through Xwayland

The user reports:
- the window is visible and clear;
- buttons and menus work with touch;
- but they are extremely small: bearable for testing, not shippable.

The user's decision: **a QML frontend is needed** in a later stage (roadmap stage 6, now a requirement rather than an option).

### Wired USB controller

The user plugged a wired controller in and ran `cat /proc/bus/input/devices > ~/input-usb.txt` in the Terminal app (written at uptime 76644 s). The file lists only the phone's own seven input devices: no USB controller appeared.

Why could not be found: the kernel log had already wrapped by the time adb was back (it started at 76960 s). Known so far:
- The kernel has host support (`CONFIG_USB_OTG`, `CONFIG_USB_DWC3_DUAL_ROLE`, `CONFIG_USB_XHCI_HCD`, `CONFIG_TYPEC`, `CONFIG_USB_ROLE_SWITCH`).
- With adb connected, the port is `data_role: host [device]`, and the only USB bus is `usb1`, a `dummy_hcd` "Dummy host controller". The real xHCI bus only appears after a switch to host role.
- stone-ut never tested OTG (only mentioned in `feasibility.md` and `roadmap-hardware-2.md` item 4).

Possible causes, not yet told apart: the port does not switch to host role (Type-C or PD handling in the port), the controller or adapter is not powered (VBUS), or the snapshot was taken before enumeration finished.

Next try: `ut/phone-debug/usb-host-probe.sh`, pushed to `/home/phablet/dolphin-ut-probe/`. It waits 5 s, then writes `~/usb-host-probe.txt` with the Type-C roles and partner, the USB device list, the input devices and the USB/HID part of the kernel log. Run it in the Terminal app with the controller plugged in:

```sh
sh /home/phablet/dolphin-ut-probe/usb-host-probe.sh
```

USB OTG is a phone-port issue (stone-ut), not a Dolphin one. A Bluetooth controller already works (stage 0 M3).

#### Second try: a pad sold as Xbox 360 compatible (2026-11-14)

The user plugged in a different controller through OTG, then ran both `cat /proc/bus/input/devices > ~/input-usb.txt` (at 20:02) and `usb-host-probe.sh` (at 20:06, uptime 78087 s) in the Terminal app. The probe's file is kept on the phone as `/home/phablet/usb-host-probe-1.txt`.

```
/sys/class/typec/port0/data_role: [host] device
/sys/class/typec/port0/power_role: [source] sink
typec partner: present
== /sys/bus/usb/devices
2-1 054c:05c4 Sony Computer Entertainment Wireless Controller speed=12
usb1 1d6b:0002 … dummy_hcd Dummy host controller speed=480
usb2 1d6b:0002 … xhci-hcd xHCI Host Controller speed=480
usb3 1d6b:0003 … xhci-hcd xHCI Host Controller speed=5000
== /proc/bus/input/devices      (only the phone's seven own devices; the same in input-usb.txt)
```

What it shows:
- **USB OTG works on this port.**
  - The Type-C port switches to host and source: the phone powers the device.
  - The real xHCI controller registers two buses (USB 2 and USB 3).
  - The controller enumerates at full speed (12 Mbit/s).
- **The pad presents itself as a DualShock 4** (`054c:05c4`, the first-generation DS4 ID), not as an Xbox 360 pad (`045e:028e`, handled by the built-in `xpad`). Many third-party pads choose the mode with a button held while plugging in.
- **No input device and no HID device were created.** The kernel has `CONFIG_HID_SONY=y` and `CONFIG_USB_HID=y`, so the device should have reached `hid-sony` or `hid-generic`. The kernel messages that would say why were already gone: the touchscreen driver logs every touch (`[FTS_TS]`), the charger logs every few seconds, and the ring buffer only reached back to 77403 s with no USB lines left in it.
- **Likely cause, not confirmed:** on Linux 5.4, `hid-sony` reads the DualShock 4's motion-calibration feature report during probe. If that request fails, the driver gives up on the device ("Failed to get calibration data from Dualshock 4"). Third-party pads that imitate a DS4 often do not answer it. Later kernels made this non-fatal; this phone runs 5.4.

`usb-host-probe.sh` now also lists each USB interface with its bound driver, the HID devices and `/dev/hidraw*`, and it filters the touchscreen and charger noise out of the kernel log.

Next tries, with the controller plugged in through OTG and the probe run in the Terminal app:
1. **Xbox 360 mode.** Switch the pad to its X-input / PC mode (see its manual; often a button held while plugging in). It should then appear as `045e:028e` and bind to `xpad`.
2. **DS4 mode again**, to catch the `hid-sony` message that confirms or rules out the calibration failure. If it is that, the fix is a kernel patch in the stone-ut port (backporting the non-fatal calibration handling), not a Dolphin change.

#### Third try: the same pad, improved probe (2026-11-14, uptime 78344 s)

The pad has no switch for an Xbox mode, so it came up as a DualShock 4 again. This time the kernel log was captured:

```
TCPC-TYPEC:** Attached.SRC        rt-pd-manager: OTG plug in, polarity = 0   enable otg mode
rt-pd-manager: pd_tcp_notifier_call source vbus 5000mV 1500mA
xhci-hcd xhci-hcd.1.auto: new USB bus registered, assigned bus number 2 / 3 (Host supports USB 3.0 SuperSpeed)
usb 2-1: new full-speed USB device number 2 using xhci-hcd
usb 2-1: New USB device found, VID=054c, PID=05c4
sony 0003:054C:05C4.0003: failed to retrieve feature report 0x81 with the DualShock 4 MAC address   (5.1 s later)
sony 0003:054C:05C4.0003: hidraw0: USB HID v81.11 Gamepad [Sony Computer Entertainment Wireless Controller] on usb-xhci-hcd.1.auto-1/input0
sony 0003:054C:05C4.0003: failed to claim input
== USB interfaces and their drivers
2-1:1.0 class=03 sub=00 proto=00 driver=usbhid
== HID devices
0003:054C:05C4.0003 (no driver bound; the probe printed "driver=driver", a script bug since fixed)
(no /dev/hidraw*, no new input device)
```

The first capture (uptime 77826 s, HID device `.0002`) had the same three `sony` lines.

**Cause, confirmed:**
- `usbhid` binds the USB interface and creates the HID device, and the built-in `hid-sony` takes it as a DualShock 4.
- While setting up the input device, `hid-sony` (Linux 5.4) reads feature report `0x81` to get the controller's MAC address. This pad does not answer that request: it times out after about 5 s.
- In this kernel that failure aborts input setup ("failed to claim input"). The probe fails, the hidraw node goes away, and no evdev device is created.
- A real DualShock 4 answers `0x81`. Clones that only imitate the DS4's input reports often do not.

**USB OTG itself works on this port.** The Type-C controller (`rt1711`) attaches as source and turns on 5 V at 1.5 A. The DWC3 switches to host, xHCI registers USB 2 and USB 3 buses, and the device enumerates. Unplugging tears it all down again and returns to device mode for adb.

Ways this pad could work, none of them Dolphin code:
1. **Kernel (stone-ut):** make a failed `0x81` read non-fatal for USB DualShock 4s in `drivers/hid/hid-sony.c` (for example, use a fixed or random MAC so the device is still registered), as a patch in stone-ut's `kernel/patches/`. This is a phone-port change and the user's decision.
2. **User space, through SDL3's HIDAPI:** Dolphin's bundled SDL3 has its own PS4 driver that can talk to the pad directly. With no kernel input driver claiming the HID device, that may work, if SDL can open the device (hidraw is gone after the failed probe, so this would need SDL's libusb path and access to `/dev/bus/usb/…`). Something to try in stage 3, when controllers are wired up.
3. **Another pad:** one that enumerates as an Xbox 360 pad (`045e:028e`, `xpad`), or any Bluetooth pad (the BETOP T6 works, stage 0 M3).

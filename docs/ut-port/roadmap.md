# Dolphin on Ubuntu Touch (stone): overview and roadmap

Written 2026-11-14, and updated the same day after the search for existing Wayland work (`wayland-prior-work.md`). Sources: this repository at `f9cb5f1c08` (upstream master), the stone-ut documents (`../stone-ut/docs/`, mainly `roadmap-hardware-2.md` items 3, 10 and 11), upstream pull requests, and a read-only probe of the phone over adb. Nothing was built, installed or changed on the phone.

Updated 2026-11-16 at the close of stage 3: stage 3's summary, stage 4 narrowed (with a spike on custom GPU drivers), stages 5 and 6 revised, and the order of the remaining stages ("Roadmap").

## Conclusion

The problem is not OpenGL versus OpenGL ES. It is X11 versus Wayland.

- Desktop Dolphin already has a GLES path. `GLContextEGL` detects the client API from the EGL configs (`Source/Core/Common/GL/GLInterface/EGL.cpp`). When the display offers no desktop GL bit but does offer GLES 3, it uses `Mode::OpenGLES` by itself. There is also the `PreferGLES` setting (`GFX_PREFER_GLES`). The OGL backend has `IsGLES()` branches throughout. Android uses the same backend in GLES mode; only the context class differs (`GLContextEGLAndroid`).
- On this phone, the only hardware GPU paths are libhybris' **Wayland** and **headless** platforms. `/usr/lib/aarch64-linux-gnu/libhybris/` has `eglplatform_{wayland,hwcomposer,fbdev,null}.so` and `vulkanplatform_{wayland,null}.so`. There is no X11 platform. Xwayland is installed, but an X11 client only gets Mesa llvmpipe.
- Dolphin on Linux renders only through X11:
  - GL: `GLContext::Create()` creates `GLContextEGLX11` for `WindowSystemType::X11`, and plain `GLContextEGL` only for Headless/FBDev. Nothing handles `WindowSystemType::Wayland`.
  - Vulkan: `VulkanLoader.h` defines `VK_USE_PLATFORM_XLIB_KHR` on Linux, and `VKSwapChain.cpp` creates only Xlib surfaces. There is no `VK_KHR_wayland_surface`.
  - Qt: `DolphinQt/Main.cpp` forces `QT_QPA_PLATFORM=xcb`, even when the user asks for wayland (upstream bug 11807: "Only the UI does, games do not launch"). `MainWindow.cpp` already fills in a Wayland `WindowSystemInfo` (`wl_display` plus `wl_surface`), but no backend uses it.

So the core of the port is a **Wayland render path**: an EGL context on a `wl_egl_window` (in GLES mode, which happens automatically) and a `VK_KHR_wayland_surface` swapchain. Qt also has to stop being forced to xcb. The rest is packaging, touch input and app-lifecycle work, the usual work for any Ubuntu Touch app.

**This render path has already been written.** Upstream pull request #14962 ("Add Wayland support (Linux only)", open, under maintainer review) adds the EGL-on-Wayland context, the Vulkan Wayland surface, surface-size reporting, Wayland keyboard and mouse input, and the Qt fixes. It merges into this fork's master with no conflicts. ROCKNIX's handheld patch set adds a NoGUI Wayland window, which is the fallback if Qt misbehaves on Mir. Stage 2 is therefore "import #14962 and adapt it to Ubuntu Touch", not "write it". Details: `wayland-prior-work.md`.

Evidence that this path works on this phone:
- GasTube, a Flutter/GTK click app, renders with Adreno GLES through `eglplatform_wayland.so` on Lomiri's `wayland-0`. See `../stone-ut/docs/external/gastube-libhybris-layout-crash.md`. That is also a warning: libhybris' Wayland window has quirks.
- libhybris' `test_vulkan` created a Wayland surface and swapchain on `wayland-0` and rendered more than 1140 frames (`roadmap-hardware-2.md` item 10).

## The target

| Item | Value | Source |
| --- | --- | --- |
| Phone | Redmi Note 12 5G, `sunstone_cn`, SM4375 (`holi`) | feasibility.md |
| CPU | 6× Cortex-A55 + 2× Cortex-A78, arm64, 4 KiB pages | feasibility.md; `getconf PAGESIZE` today |
| RAM / free storage | 7.3 GiB (+2 GiB swap) / 91 GB free on `/` | adb today |
| GPU | Adreno 619v2, KGSL only, 266–700 MHz. `mod_percent=200` set at boot | roadmap-hardware-2.md items 10, 11 |
| GLES | 3.2, `V@0530.0` (2022 vendor blob), through libhybris → `libEGL_adreno.so`, EGL 1.5 | item 10 |
| Desktop GL | Not in hardware. `EGL_CLIENT_APIS` = `OpenGL_ES` only. Mesa gives llvmpipe | item 10 |
| Vulkan | 1.1.128, driver 512.530.0, through `libvulkan1-hybris` (`libvulkan.so.1.2.183`, libhybris' own loader, no Khronos ICD directory). `VK_KHR_wayland_surface` present | item 10 |
| Display | 1080×2400, 120 Hz (60 Hz selectable). Lomiri is a nested Mir 1.8 server on `lomiri-system-compositor` | item 11 |
| OS | Ubuntu Touch 24.04-2.x (`Ubuntu 24.04.5`), Halium 14, kernel 5.4.283 `#20` | adb today |
| Qt | Qt 6.10.2 from UBports (`libqt6widgets6`, `libqt6gui6`, `libqt6opengl6`, `libqt6waylandclient6`). Platform plugins in `qt6/plugins/platforms/` include `libqwayland.so` and `libqxcb.so` | adb today |
| Sockets for phablet | `/run/user/32011/wayland-0`, `mir_socket`, `/tmp/.X11-unix/X0` (Xwayland) | adb today |
| Audio | PulseAudio 16.1 (`libpulse0`), ALSA libs | adb today |
| Input nodes | `/dev/input/event*` are `root:android_input 0660`. phablet is in `android_input` | adb today |
| Not installed | gcc/g++, cmake, Khronos `libvulkan1`, `/usr/share/vulkan/icd.d` | adb today |

## How Dolphin's paths map onto this phone

| Dolphin path | Android build | Linux build today | Needed on Ubuntu Touch |
| --- | --- | --- | --- |
| Window system | `WindowSystemType::Android` (`ANativeWindow`) | X11 (Qt forced to xcb) | **Wayland** (Qt `wayland` plugin on Lomiri) |
| OpenGL backend | GLES through `GLContextEGLAndroid` | Desktop GL through `GLContextEGLX11`. GLES only if the driver lacks GL, or with `PreferGLES` | GLES through a new **EGL-on-Wayland context**. Mode detection already picks GLES |
| Vulkan backend | `VK_KHR_android_surface`. Optional custom drivers through libadrenotools | `VK_KHR_xlib_surface`, system `libvulkan.so.1` | **`VK_KHR_wayland_surface`**, system `libvulkan.so.1` (the hybris loader). No adrenotools: it needs Android's linker |
| Headless / FBDev | — | `GLContextEGL` | Works as-is for early tests (no window) |
| CPU emulation | arm64 JIT | arm64 JIT (`_M_ARM_64`) | Same code. 4 KiB pages, so fastmem should behave as on any arm64 Linux |
| Input | Touch overlay (Java) → `InputOverrider` (C++, `InputCommon/ControllerInterface/Touch`) | evdev, SDL3, Xlib | Gamepads: evdev/SDL3. Touch overlay: new frontend code driving the same `InputOverrider` |
| Audio | OpenSL ES / cubeb | PulseAudio, ALSA, cubeb | PulseAudio or cubeb (pulse) |
| GPU driver quirks | `DriverDetails.cpp`: `OS_ANDROID` + `VENDOR_QUALCOMM` entries | `OS_LINUX` | The blob is the Android one, but Dolphin will report `OS_LINUX`. See risk 6 |
| Frontend | Kotlin app, touch-first | Qt Widgets, mouse and keyboard | A Qt Quick (QML) reimplementation of the Android layout (stage 3). DolphinQt kept for debugging |

## How the port is put together

```
 Dolphin (Qt6 Widgets, QT_QPA_PLATFORM=wayland)
   ├── UI windows ─────────────── Qt wayland plugin ──┐
   └── render window (QWindow)                        │
         wl_display + wl_surface (MainWindow.cpp)     │
           ├── OGL:    wl_egl_window → EGL (GLES 3.2) │
           └── Vulkan: vkCreateWaylandSurfaceKHR      │
                 │                                    ▼
          libhybris eglplatform_wayland /     Lomiri (nested Mir 1.8, wayland-0)
          vulkanplatform_wayland                      │
                 │                                    ▼
          /vendor Adreno blobs → KGSL       lomiri-system-compositor → HWC 2.4 → panel
```

Dolphin's emulation core does not change. The port only touches host-side code: GL context creation, the Vulkan surface, the Qt platform choice, input frontends, build files and packaging.

## Rules for this fork

- `AGENTS.md` and `Contributing.md` apply. Upstream does not accept pull requests from AI agents. AI agents must not make changes related to the behavior of the emulated consoles. Agent commits carry 🤖 in the message.
- Work in this fork that an agent does stays host-side (window system, GPU API plumbing, build, packaging, input frontend, docs). Anything that changes how the emulated GameCube/Wii behaves is out of scope for agents. That includes the Qualcomm `DriverDetails` entries in risk 6, because one of them changes the generated pixel shaders. The user decides on those and writes them.
- Anything this port offers upstream (for example Ubuntu Touch fixes to the Wayland path) has to be authored and submitted by the user, with AI-generated parts disclosed, as `Contributing.md` requires.
- Imported upstream work (#14962) keeps its commits and authors, and is not edited in place. This port's changes to it go in separate commits on top, so the import can be swapped for the merged upstream version later. Problems found in it can be reported on the PR by the user, as a human.
- Keep port changes small and in separate commits, so they can be rebased onto new upstream masters. Put scripts in a separate directory (for example `ut/`), not mixed into `Source/`.
- Live tests run on the phone first. The user flashes, installs and commits, as in stone-ut.

## Roadmap

Each stage has a goal, planned steps and an exit check. Later stages add "Implementation notes" and "Events" sections, as in the stone-ut documents.

**Order from here (2026-11-16): stage 5 next, then stage 4, then stage 6.**
- **Stage 5 (packaging)** turns the development entry into an installable app, and it holds the largest unknowns: gamepads under confinement, the JIT and the CPU boost under AppArmor, and content-hub import as a confined app. What it finds may change code, so it comes before polishing.
- **Stage 4 (narrowed)** is no longer a broad measurement campaign: stage 3 settled speed and defaults. What remains is Wind Waker's Vulkan hang, a compatibility table, a long run as an installed app, frame pacing and the custom driver spike. The Wind Waker steps can start any time if it bites first.
- **Stage 6** collects the features left out of stage 3: the 3.6 second pass and the deferred Android features.

### 0. Probe the phone (no Dolphin code)

Goal: confirm the pieces Dolphin will lean on, before writing any code.

Planned steps:
1. **Native Qt6 Wayland on Lomiri.** Run a trivial Qt6 Widgets program (and one with a `QOpenGLWidget`) with `QT_QPA_PLATFORM=wayland`, `XDG_RUNTIME_DIR=/run/user/32011`, `WAYLAND_DISPLAY=wayland-0`. Check that it gets a window, takes touch input, and that `QGuiApplication::platformName()` is `wayland`. Mir 1.8's Wayland frontend is older than desktop compositors, so check which shell protocols Qt picks (`WAYLAND_DEBUG=1`).
   - Record the compositor's globals from that log: `xdg_wm_base` (ROCKNIX's NoGUI window needs it), `wl_subcompositor`, and the optional protocols #14962 binds (`wp_content_type_manager_v1`, `zwp_pointer_constraints_v1`, `zwp_relative_pointer_manager_v1`, `wp_pointer_warp_v1`).
   - Record what a click app is launched with. Session services have `QT_QPA_PLATFORM=ubuntumirclient;wayland-egl;xcb`, and Qt 6 here ships the `ubuntumirclient` plugin, which Dolphin does not know (`wayland-prior-work.md`, "Ubuntu Touch findings").
2. **A second top-level window.** Dolphin renders into its own `QWindow` (or the main window with "Render to Main Window"). Check that a second top-level surface goes fullscreen and stays on top under Lomiri. Check whether native child windows (Wayland subsurfaces) are composited at all.
3. **GLES as Dolphin will use it.** Extend `../stone-ut/tools/phone-debug/gpu-probe.py`, or use `libhybris-test`'s tests, to list `GL_EXTENSIONS` and limits on a Wayland window surface. Check what Dolphin's `OGLConfig.cpp` queries: `GL_EXT_blend_func_extended`, `GL_EXT_buffer_storage`, `GL_EXT_shader_framebuffer_fetch`, geometry shaders, `GL_EXT_texture_buffer`, `GL_EXT_clip_cull_distance`, compute shaders, and so on. Also test shared contexts created on a second thread, since Dolphin compiles shaders on worker threads.
4. **Vulkan as Dolphin will use it.** Device features (`dualSrcBlend`, `geometryShader`, `fragmentStoresAndAtomics`, `logicOp`, and so on), the surface's present modes (FIFO / MAILBOX / IMMEDIATE) and formats, and the full instance extension list. Check whether `VK_KHR_get_physical_device_properties2` and `VK_KHR_get_surface_capabilities2` are present (Dolphin adds them as optional).
5. **Gamepads.** Pair a Bluetooth controller and plug in a USB one. Check that they appear as `/dev/input/event*` (kernel HID and `uhid` support), with which permissions, and whether udev tags them as joysticks.

Exit: a short table of yes/no answers in this document. Any "no" in steps 1, 3 or 4 changes stage 2.

#### Status (2026-11-14): done, except the Qt 6 Widgets window (moved to stage 1 step 5)

Notes, logs and the probe: `stage0-probe.md` and `ut/phone-debug/wayland-probe.py`. The automated probes ran with the screen off. The user ran the on-screen and gamepad tests (M1–M3) the same day.

| Step | Answer | What it changes |
| --- | --- | --- |
| 1 Qt 6 on Wayland | **Not as installed.** No Qt 6 shell integration plugin is on the phone. With `libxdg-shell.so` from `libqt6wlshellintegration6`, Qt 6 uses xdg-shell + Wayland-EGL and creates its toplevel. A Widgets window is not tested yet (no test binary; moves to stage 1) | The click must bundle `libxdg-shell.so` + `libQt6WlShellIntegration.so.6` (stage 5). Stage 1 builds a Qt 6 Widgets test program |
| 1 Globals | `xdg_wm_base` v1, `zxdg_shell_v6`, `wl_shell`, `wl_subcompositor`, `android_wlegl`. None of #14962's optional protocols (content-type, pointer constraints, relative pointer, pointer warp) | #14962 must tolerate their absence (stage 2) |
| 1 Click environment | `QT_QPA_PLATFORM=ubuntumirclient;wayland-egl;xcb`, confirmed on a launched click app | Stage 2 step 3 as planned |
| 2 Toplevel / fullscreen | Windowed 1080x2305 (below the 95 px panel). Fullscreen 1080x2400. Both shown on screen (M1) | Dolphin's render window should go fullscreen for the whole panel |
| 3 GLES | **Yes**, also on screen (M1). `eglGetDisplay(wl_display)` reaches `eglplatform_wayland.so` with no environment. GLES 3.2, 98 extensions, shared contexts on a second thread, window surface resize and recreate all work | Swap-interval-1 pacing is 83–87 fps on the 120 Hz panel, not vsync-locked. Frame pacing goes to stage 4 |
| 4 Vulkan | **Yes.** `VK_KHR_wayland_surface`, swapchain and acquire work. API 1.1.128. No `logicOp`, no fragment-stage subgroups, no BC textures, no `VK_EXT_memory_budget` | **`currentExtent` is 1x1, not `0xFFFFFFFF`**: #14962 would create a 1x1 swapchain. Port fix in stage 2. `test_vulkan` presents on screen (M2) |
| 5 Gamepads | A Bluetooth BETOP T6 (Xbox mode, `045e:02e0`) binds `hid-microsoft`: joystick-tagged, rumble, and phablet can open it (M3). Buttons come through as generic `BTN_A`…`BTN_TR2` + `KEY_MENU`. USB not tested. `hid-wiimote` is built in | Default mappings in stage 3 (SDL3's database knows `045e:02e0`). `/dev/input` access for a confined click remains a stage 5 question |

Still open from stage 0: the Qt 6 Widgets window (stage 1 step 5), a wired USB controller (Ubuntu Touch has no wireless adb, so it needs the phone's Terminal app; see `stage1-build.md`), and which physical button gives which code on the BETOP T6 (stage 3).

### 1. Build environment and a CPU-only run

Goal: an arm64 Dolphin built against the phone's libraries, running the emulation core on the phone without a window.

Planned steps:
1. Choose the build route. Recommended: build in an arm64 Ubuntu 24.04 container that has UBports' 24.04-2.x repository. clickable's 24.04 images are one way. Qt must match the phone's 6.10.2, not noble's 6.4. A native build on the phone (6+2 cores, 7 GiB, 91 GB free) also works for experiments, but it needs a compiler on a rootfs that the boot overlay rewrites. Keep it in a Libertine container or a chroot.
2. Configure with only what the phone needs: `ENABLE_QT=ON`, `ENABLE_VULKAN=ON`, `ENABLE_EGL=ON`, `ENABLE_X11=OFF` once stage 2 lands (`ON` until then), `ENABLE_LLVM=OFF`, `ENABLE_EVDEV=ON`, `ENABLE_PULSEAUDIO=ON`, `ENABLE_ALSA=ON`. Use the bundled `Externals/` for everything except Qt6, Wayland, EGL, libevdev, libudev and PulseAudio. With #14962 (stage 2) the container also needs `wayland-scanner` and xkbcommon: without them, Wayland input is not built and Qt falls back to xcb, which has no GPU on this phone.
3. Run `dolphin-emu-nogui` with the Headless platform and the Null or Software renderer on a homebrew or a dump the user owns. This checks the arm64 JIT, fastmem, audio output and the save paths on this kernel before any GPU work.
4. Record build times, binary size and the runtime dependency list. The list feeds stage 5.
5. **Qt 6 Widgets test program** (left over from stage 0 step 1). Build a small Qt 6 Widgets program with one `QOpenGLWidget`. Run it on the phone with `QT_QPA_PLATFORM=wayland` and `QT_PLUGIN_PATH` pointing at the unpacked `libxdg-shell.so` (`/home/phablet/dolphin-ut-probe/qt6w/`). Check that it maps, renders through GLES and takes touch input. Also check a second top-level window and a native child widget (a subsurface).

Exit: a game boots headless on the phone, and the JIT runs with no faults. The Qt 6 Widgets test window works, or the NoGUI fallback (stage 2 step 7) becomes the plan.

#### Status (2026-11-14): done

Notes: `stage1-build.md`.

| Step | Result |
| --- | --- |
| 1 Build route | Our own cross container, `ut/docker/` (amd64 Ubuntu 24.04 + arm64 multiarch + UBports **24.04-2.x**, pinned as on the phone). clickable's 24.04 image follows 24.04-1.x, and an emulated arm64 build would be much slower. GCC 13.3, CMake 3.28.3, Qt 6.10.2 (the phone's exact build) |
| 2 Configure | `ut/build.sh`, `USE_SYSTEM_LIBS=OFF` (all optional deps from `Externals/`). Qt, X11, EGL, evdev/udev, ALSA, PulseAudio and BlueZ from the system |
| 3 Run on the phone | **A Wii game boots headless and runs** (S1-M1): arm64 JIT, IOS, apploader, the game's OS reports, AXWii audio through Cubeb/PulseAudio, a save written, no faults. Headless OpenGL ES on the Adreno renders the game correctly (checked from dumped frames). **All 1055 Dolphin unit tests pass on the phone**, including the arm64 JIT emitter and the page-fault handler fastmem needs. DolphinQt's UI works over Xwayland with touch, but it is far too small to ship (S1-M3) |
| 4 Build facts | Clean build 2 min 40 s. Binaries 34/28/28 MB, package 40 MB (106 MB unpacked). Every `NEEDED` library is already on the phone |
| 5 Qt 6 Widgets | Works on native Wayland with the bundled xdg-shell plugin (S1-M2): widgets drawn, touch and the maliit keyboard work, a fullscreen second window covers the whole 1080x2400 panel, and DolphinQt's `wl_display`/`wl_surface` lookup works. Qt's own GL logs EGL config errors but still draws |

Also found: apps outside the Lomiri session need `GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096` and `LD_PRELOAD=libtls-padding.so`, or libhybris fails to load (`ut/phone-debug/ut-env.sh`). Qualcomm `core_ctl` parks idle little cores; the A78s are always available.

Carried forward:
- Dual core (`CPUThread`) is on by default only on Android. 3.0 measured it: dual core beats single core on this phone (79 % against 41 % in a Need for Speed race, with the CPU boost), so the port's default `Dolphin.ini` sets it (3.8; decided by the user).
- The GPU sat at 266 MHz while 100 % busy in one run (stage 4). In 3.0's benchmark the CPU clock turned out to be the first limit, and the GPU reached its top clocks once the CPU was boosted.
- Before a QML frontend: clear Qt's EGL config errors (stage 6).
- Wired controllers: **USB OTG works** (Type-C source with 5 V/1.5 A, xHCI host, the pad enumerates). The second pad is a DualShock 4 clone (`054c:05c4`) that the 5.4 kernel's `hid-sony` rejects: it does not answer feature report `0x81` (the MAC address), so no input device is created (confirmed from the kernel log). Options: a `hid-sony` kernel patch in stone-ut, SDL3's HIDAPI PS4 driver over libusb (stage 3), or an X-input or Bluetooth pad. Bluetooth pads work.

### 2. Wayland rendering, from upstream PR #14962

Goal: games render on screen with the hardware GPU, through both backends.

The render path itself is not written here. It comes from upstream PR #14962 (`wayland-prior-work.md`), pinned at `5003b700ffecffde62fb8fe793921512a44feb5e` (2026-11-07). That PR merges into this fork's master with no conflicts. This stage imports it, fills the gaps specific to Ubuntu Touch, and tests it on the phone.

Planned steps:
1. **Import.** Create a branch `ut/wayland` from master. Cherry-pick the PR's 17 commits in order (`git cherry-pick 3eb30c7620..5003b700ff`, from the fetched `refs/remotes/pr/14962`), keeping their authors. Record the pinned hash in this section. Do not squash or edit them: they will be replaced by the upstream version when it merges.
2. **Build it** in the stage 1 container with `ENABLE_WAYLAND=ON`, and check that CMake reports "Wayland keyboard and mouse input enabled" (it needs `wayland-scanner` and xkbcommon). Without that, Qt is forced back to xcb.
3. **Qt platform selection.** Make sure Dolphin runs on `wayland`, never on the session's `ubuntumirclient`. Set `QT_QPA_PLATFORM=wayland` in the click launcher, and add a small port commit in `DolphinQt/Main.cpp` that replaces an `ubuntumirclient` entry with `wayland`. Keep `QT_WAYLAND_DISABLE_WINDOWDECORATION=1`, as Lomiri does.
4. **Bring up GLES first, then Vulkan.** GLES is the path Android Dolphin uses most on Adreno. Stage 0 confirmed that the PR's `eglGetDisplay()` on the `wl_display` reaches `eglplatform_wayland.so` through glvnd with no extra environment. Then Vulkan on the 1.1.128 driver through the hybris loader.
   - **Port commit: the swapchain size.** libhybris reports `currentExtent` 1x1 for a Wayland surface (stage 0). #14962 only uses the window's size when the extent is `0xFFFFFFFF`. On `WindowSystemType::Wayland`, always take the size from `m_wsi.render_surface_width/height`.
   - Check that Dolphin's surface format choice accepts `R8G8B8A8_UNORM`: this surface offers no `B8G8R8A8`.
   - Check that the optional protocol binds (content-type, pointer constraints, relative pointer, pointer warp) are skipped cleanly: Lomiri offers none of them.
   - Lomiri's first configure is 0x0, and the real size (1080x2305, or 1080x2400 fullscreen) comes later. The surface must follow the later configures.
5. **libhybris Wayland quirks.** Resize, rotation, fullscreen toggles and surface recreation are where GasTube found trouble in libhybris' `WaylandNativeWindow`. Test them deliberately. Do not copy GasTube's offset-based workaround. Swap interval and frame pacing through two compositors also belong here: Mir paces at the panel rate, 120 Hz by default.
6. **Known PR bugs, checked on Lomiri:**
   - washed-out colors (seen on KDE Plasma): compare a known scene with Android screenshots or the Software renderer;
   - "Render to Main Window" with fullscreen (a subsurface role error, fixed in the PR on Sway);
   - the splitter that cannot be dragged during a game;
   - global shortcuts with no XDG desktop portal present.
   Fixes this port needs go in separate commits on top of the import (see "Rules for this fork").
7. **Fallback, only if Qt windows do not work on Mir.** Port ROCKNIX's `015-wayland-nogui-platform.patch` (a fullscreen `xdg_toplevel` for `dolphin-emu-nogui -p wayland`) on top of #14962's EGL and surface-size code. Do not apply ROCKNIX 011–014 next to #14962, since they overlap. It needs `xdg_wm_base` from stage 0.
8. **Track upstream.** Watch #14962. Its history will be squashed and force-pushed before it merges. When it merges, drop the imported commits, rebase this port onto upstream, and keep only the port's own commits.

Exit: one game runs on screen in each backend. Resizing or rotating, and leaving and re-entering fullscreen, do not crash.

#### Status (2026-11-14): done (closed by the user). Both backends and the main UI with a separate render window work on screen; Render to Main Window is avoided on Lomiri

Notes: `stage2-wayland.md`.

| Step | Result |
| --- | --- |
| 1 Import | Branch `ut/wayland`. #14962 at `5003b700ff` merged with `git merge --no-ff --no-commit`, no conflicts, 61 files staged. A merge instead of cherry-picks: the PR's 17 commits stay unedited with their authors, the user makes the commits, and `AGENTS.md`'s 🤖 rule is not at odds with "do not edit them". **Not committed yet**; the commit order is in `stage2-wayland.md` |
| 2 Build | "Wayland support enabled", "Wayland keyboard and mouse input enabled". Clean build 2 min 40 s. Adds `libwayland-client` and `libxkbcommon` (on the phone) |
| 3 Qt platform | Port fix in `DolphinQt/Main.cpp` drops `ubuntumirclient` from `QT_QPA_PLATFORM`. Checked: started with the session's value, Dolphin loads `libqwayland.so` + xdg-shell |
| 4 GLES, Vulkan | Both run the game through Qt's Wayland window on the phone (screen off): GLES 3.2 via `eglplatform_wayland.so`, Vulkan via `VK_KHR_wayland_surface`. Port fix in `VKSwapChain.cpp`: always use the window size on Wayland (libhybris reports `currentExtent` 1x1). The missing content-type protocol is handled. On screen: S2-M1/M2 |
| 5–6 Quirks, PR bugs | S2-M1 (OpenGL ES): picture right, colors good, fullscreen covers the panel, rotation follows; speed about 45 %. Fixed: landscape at launch (`RenderWidget.cpp`); the volume keys (`PULSE_PROP`) and the media volume being reset to 100 % at every start (`CubebStream.cpp`). S2-M2 (Vulkan): swap chain re-creation hung inside libhybris; fixed by rebuilding the `VkSurface` on Wayland (`VKSwapChain.cpp`); passes, 55–110 % speed. S2-M3 (main UI, separate render window): **passes**, including fullscreen and Stop. S2-M4 (Render to Main Window): one intermittent SIGSEGV on fullscreen (not repeated in two re-runs; crash capture in the launcher), fullscreen shows as a second app (DolphinQt's design), and after Stop Lomiri kept the destroyed subsurface's last frame on screen (the client side is correct). **Decision: avoid Render to Main Window on Lomiri** (keep `RenderToMain = False`) rather than fix it; no subsurfaces for the game picture in the QML frontend either. The Wii Remote mapping window is larger than the screen: deferred to the QML frontend |
| 7 Fallback | Not needed so far |

Found:
- Stopping waits on Qt's **Confirm on Stop** dialog: it is not a hang. Stage 3 decides stop and pause behavior.
- **Screensaver inhibit fails** (`org.freedesktop.ScreenSaver` has no `Inhibit` on Ubuntu Touch): the display may dim during play (stage 3).
- **Volume keys:** Dolphin's PulseAudio stream had no media role, so Ubuntu Touch's role-based volume (the keys change the multimedia role) never applied, and it played loud. Fixed with `PULSE_PROP="media.role=multimedia"` in the launcher; the click launcher must set it too (stage 5).

### 3. A touch-first QML frontend (replaces "Usable on a phone")

Goal: someone can pick up the phone and play, without a keyboard and mouse, in a frontend modeled on the Android app.

**Changed 2026-11-14 (user decision):** the desktop Widgets UI is not adapted. A reimplementation of the Android app's layout in Qt Quick (QML) replaces it, and the QML item of stage 6 moves here. Full sub-roadmap: **`stage3-roadmap.md`**.

| Phase | What |
| --- | --- |
| 3.0 Foundations and window spike | `Source/Core/DolphinUT/` (new frontend, target `dolphin-ut`), QML shell with an OpenGL ES surface format, `Host_*` functions; decide how the game picture and the touch overlay share the screen without a Wayland subsurface (recommended: overlay drawn by Dolphin's on-screen UI); Lomiri checks (orientation, keep-display-on, app state, haptics, keyboard) |
| 3.1 Game library | cover grid with platform tabs, details, folders, over `UICommon::GameFileCache` |
| 3.2 Emulation screen and lifecycle | boot, resize and rotation, landscape lock, keep-display-on, pause on suspend, resume |
| 3.3 Touch overlay | GameCube, Wii Remote, Nunchuk, Classic layouts with the Android button graphics, through `InputOverrider`; edit mode, scale, opacity, haptics |
| 3.4 In-game menu | Android's `MenuFragment`: pause, screenshot, save/load states, overlay, settings, Wii Remotes, disc, reset, exit (no Confirm on Stop) |
| 3.5 Controllers and mapping | SDL3 and evdev, default mappings for known pads, an Android-style mapping UI and profiles |
| 3.6 Settings | a settings model over `Config::Info`; a first pass of the settings that matter on a phone, in the Android menu structure; per-game settings |
| 3.7 Files and storage | confined user folder, game import through content-hub, MTP and microSD |
| 3.8 Defaults for this phone | dual core, Vulkan (OpenGL ES fallback), launcher environment; settings that change emulated behavior stay upstream's |

Removed from the old stage 3: Widgets UI scaling, `RenderToMain` as a shipped default, Confirm on Stop handling. Deferred to after stage 3: cheats, Riivolution, Skylanders and Infinity Base, NetPlay, achievements, GBA link, disc conversion, system update, TV layout, translations of the new strings.

Exit: from a fresh install, a GameCube and a Wii game start from the library, play with touch and with a Bluetooth gamepad, survive switching apps and rotation, save and load states from the in-game menu, and exit to the library, with no stale frames, hangs or crashes.

#### Status (2026-11-16): done; the 3.6 second pass moved to stage 6

Notes: `stage3-roadmap.md` (phases, decisions, "Stage 3 closed") and one document per phase, `stage3.0-foundations.md` to `stage3.8-defaults.md`.

| Phase | Result |
| --- | --- |
| 3.0 Foundations | `Source/Core/DolphinUT/` (target `dolphin-ut`), a QML shell on OpenGL ES. **Window architecture A** (user): the touch overlay and menus are drawn by Dolphin's renderer into the game picture, no subsurface. Benchmark: the CPU clock is the limit; uclamp_min on the emulation threads doubles the speed, and dual core beats single core |
| 3.1 Game library | A native Lomiri library with the Android tabs, GameTDB covers, details, grid options and game folders |
| 3.2 Emulation screen | A fullscreen game window (Lomiri needs `set_fullscreen` after mapping, with no other window of the app showing), landscape, keep-display-on, pause on app switch and screen lock, vibration |
| 3.3 Touch overlay | The Android layouts and graphics through `InputOverrider`, and the Android customization (edit mode, toggles, latching, scale, opacity). Portrait puts the game in the top half |
| 3.4 In-game menu | The Android menu as a panel in the picture: pause, screenshot, save states, overlay, Wii Remotes, disc, reset, exit |
| 3.5 Controllers | Controllers pages after the Android app's, input detection, profiles, SDL gamepad defaults, automatic mapping of a new pad, rumble |
| 3.6 Settings (first pass) | The Android settings menus generated from its sources, bound to Dolphin's own config objects; per-game settings; the CPU Boost setting; Dolphin's alerts logged, shown and ignored instead of ending the app |
| 3.7 Files and storage | Games in ~/Documents/Dolphin/Games (reachable over MTP and by a click), the user folder in the app's own data folder, screenshots in ~/Pictures/Dolphin, content-hub import and share, file and folder pickers, path settings, a development entry in the app drawer |
| 3.8 Defaults | Dual core, save states and covers on by default, written once per user folder and never over the user's values; Vulkan, with OpenGL ES when Vulkan fails to start |

Speed at the close: Need for Speed: Most Wanted, the hardest game tested, went from 50–60 % (single core, no boost, start of stage 3) to 100 % of full speed with dual core and the boost. Newer SMB runs at full speed.

User decisions: window architecture A; the native Ubuntu Touch look (Lomiri Components); a CPU boost setting, on by default (heat is not an issue); dual core by default; per-game settings; all of the Android app's features, or more; covers on; save states on.

Carried forward:
- **Wind Waker hangs the GPU on Vulkan** (`VK_ERROR_DEVICE_LOST`, KGSL "GPU hang detected"), in DolphinQt too; OpenGL ES works. Vulkan is the default, so the game hangs out of the box until stage 4 settles it. Meanwhile, its game settings can choose OpenGL ES.
- Nothing has run confined yet: the development entry is unconfined (stage 5).
- The 3.6 second pass and the deferred Android features (stage 6).

### 4. Performance and correctness on Adreno 619 (narrowed 2026-11-16)

Goal: know what runs well, fix or work around what does not, and decide on custom GPU drivers.

**What stage 3 already settled.** This stage was planned as a broad measurement campaign. Stage 3 answered its main questions on the way:
- the CPU clock was the limit, and the CPU boost (uclamp_min 1024, no root) doubles the speed (3.0, 3.6);
- dual core beats single core, and is the default (3.0, 3.8);
- Vulkan is the default, with OpenGL ES when Vulkan fails to start, and per game in the game settings (3.6, 3.8);
- Need for Speed: Most Wanted, at 50–60 % in races at the start of stage 3, runs at full speed.

So the stage is not skipped, but it shrinks to the steps below. Steps 1 and 2 matter most: a game that hangs out of the box is the one known bug a new user would hit.

Planned steps:
1. **Wind Waker on Vulkan** (`GZLP01`, found in 3.1): the GPU hangs about 10 s in (KGSL "GPU hang detected", three faults, then the context is dropped), in DolphinQt too. OpenGL ES works.
   - Ruled out: ubershaders, the EFB/XFB copy and access options, backend multithreading, fog. The Android-only Qualcomm entries are unlikely to cover it: `BUG_BROKEN_TEV_COLOR_ZERO` starts at driver 700, and this phone has V@530.
   - Next: bisect Dolphin's Vulkan feature use (dual-source blending, logic ops, framebuffer fetch, …); check the game's own GFX settings; try a custom driver (step 6); compare with Android Dolphin on the same driver (Waydroid, not set up) if still unclear.
   - Host-side, whatever the cause: check what the app does today when the device is lost during play, and make it end the game cleanly and offer OpenGL ES for that game (a per-game setting), instead of hanging.
   - A fix that changes how Dolphin draws the game (shader generation, `DriverDetails` entries) is the user's decision and change (risk 6, "Rules for this fork").
2. **The backend for known problem games, out of the box.** If step 1 finds no Vulkan fix, the port starts known problem games on OpenGL ES by default: a per-game backend choice, host-side, set the way 3.8 sets its defaults. The user decides.
3. **A small compatibility table.** The user's games on both backends, from save states (`--state-slot`, the `perf:` log line), in dual core with the boost. It feeds step 2 and the OpenStore description.
4. **A 15-minute run as an installed app.** Started from the app drawer (Lomiri's cgroup, not adbd's): temperature, battery drain, CPU and GPU clocks with the boost at 1024. Settle the boost value, and whether the GPU governor (`mod_percent`) needs a hint.
5. **Frame pacing.** 60 Hz games on the 120 Hz Mir output against the 60 Hz system setting (`stone-refresh-rate 60`), with `../stone-ut/tools/phone-debug/fps-panel.py`.
6. **Custom Vulkan drivers: a spike** (user request, 2026-11-16). The Android app lets users install a driver, for example a Mesa Turnip build from K11MCH1/AdrenoToolsDrivers, and loads it through libadrenotools, which hooks Android's linker namespaces. That library cannot work under libhybris. Found on the phone:
   - Vulkan goes from `libvulkan.so.1` (libhybris) to Android's own loader `/system/lib64/libvulkan.so`, then to the vendor driver `/vendor/lib64/hw/vulkan.adreno.so`, chosen by the system property `ro.hardware.vulkan=adreno`. Presentation on Wayland goes through `vulkanplatform_wayland.so`, which gives Android's loader a native window.
   - libhybris' Vulkan settings are only `HYBRIS_VULKANPLATFORM` and `HYBRIS_VULKANPLATFORM_DIR`: nothing selects another driver for one app.

   Routes, in order:
   - **Turnip's Android build, loaded by libhybris in place of the vendor driver, for this app's process only.** Android's loader and hybris' Wayland presentation stay as they are. It needs a way to redirect the driver load in one process: a libhybris hook, or the "updatable driver" namespace of Android's loader, which is what adrenotools uses on Android.
   - **A native (glibc) Mesa Turnip with its KGSL backend.** No libhybris, and Vulkan 1.3. But Mesa presents on Wayland through `linux-dmabuf`, which Lomiri does not offer (stage 0's globals), so frames would have to reach the screen another way, for example a dma-buf imported into the hybris EGL surface. Larger.
   - Replacing the vendor driver for the whole system: needs root, affects every app, and a click cannot do it. Not pursued.

   Also check that Turnip supports the A619 on this kernel's KGSL (5.4).

   If the first route works, the feature can follow the Android app: a "GPU driver" setting (global and per game), a driver zip that the user picks with the file picker, and the system driver as the default. The app does not bundle or download drivers. They are third-party binaries, and some in that repository are Qualcomm's, which cannot be redistributed.

   The spike ends with a yes or no, and what a yes would take.
7. **Risk 6.** List where this phone's blob shows the bugs that `DriverDetails.cpp` guards only on Android, and report to the user.

Exit:
- Wind Waker runs on Vulkan, or starts on OpenGL ES by default;
- a results table for the tested games;
- a verdict on custom drivers.

#### Status (2026-11-16): done; only the user's `DriverDetails` line is open

All on-screen tests passed (S4-M1…M6). S4-M5: frame pacing is the same at 60 Hz and 120 Hz, so the panel stays at 120 Hz.

- **S4-M2 passed:** marking dual-source blending broken gives no hang and nothing broken.
- **S4-M3 passed:** a lost GPU restarts the game on OpenGL ES.
- **S4-M4 passed** on OpenGL ES (the game's own setting): about 18 minutes at ~99% with no slowdown, about 2% battery in 16 minutes, barely warm.
- **S4-M1:** Turnip's picture is right. Every Turnip game crashed when it stopped (SIGBUS in `vkDestroyDevice`). The cause: Mesa calls `pthread_barrier_*`, which libhybris does not hook. Fixed in `VulkanDriver.cpp`, which supplies glibc's barrier functions through `hybris_set_hook_callback`.
- **The GPU driver feature is built** (step 8): Settings → Graphics → GPU Driver, as on Android. It installs AdrenoTools packages (zip with `meta.json`), chooses one for the next start, and removes them. The click 2609.266 ships the shim, which now builds without files from a phone. Checked over adb and on screen in the installed click (S4-M6).
- **Open:** the `DriverDetails` line for dual-source blending is the user's decision and change. The candidate and its version range (`0x80000000`–`0x80213000`) are in the notes.

Notes: `stage4-adreno.md`.
- **Wind Waker's hang is dual-source blending** in Qualcomm's V@530 Vulkan driver (bisected over Dolphin's optional Vulkan features). Marking it broken (`BUG_BROKEN_DUAL_SOURCE_BLENDING`, Dolphin's existing mechanism) or switching it off removes the hang. That `DriverDetails` line is the user's decision and change.
- **A lost GPU no longer leaves a frozen game:** at the first `VK_ERROR_DEVICE_LOST` the game stops and starts again on OpenGL ES, with a note (one alert instead of thousands).
- **Custom Vulkan drivers work under libhybris** (step 6: yes), with no root. A 7.7 KB stand-in `libvndksupport.so` (`ut/vkdriver/`, built without the NDK) lets Android's Vulkan loader load a driver from a folder the app chooses at start. Mesa Turnip v26.0.0 R8 runs Wind Waker with no hang, and Need for Speed at 100 %. The settings page and installer are built (step 8).
- Compatibility table, the Android-only Qualcomm entries (risk 6), and scripts for the long run and frame pacing: in the notes.

### 5. Packaging and distribution

Goal: an installable click package.

Planned steps:
1. A clickable project (`clickable.yaml`, CMake builder, arm64, the framework the phone reports; it runs 24.04-2.x). The app it launches is the QML frontend `dolphin-ut` (stage 3); DolphinQt is not shipped. Bundle Dolphin's `Sys/` data and the libraries the system does not provide. That includes Qt 6's `wayland-shell-integration/libxdg-shell.so` and `libQt6WlShellIntegration.so.6` from UBports' `libqt6wlshellintegration6` (same Qt 6.10.2), with `QT_PLUGIN_PATH` set by the launcher. The system has no Qt 6 shell plugin (stage 0).
2. AppArmor. The policy groups follow from stage 3 (`stage3.7-storage.md`; the phone has policy version 2404.2):
   - `audio`, `keep-display-on`, `networking` (GameTDB covers);
   - `content_exchange`, `content_exchange_source` (Import Games, Share);
   - `document_files` (~/Documents/Dolphin/Games, `<card>/Documents`), `picture_files` (screenshots).

   Check under the real profile: content-hub import (the rename or hard link out of `HubIncoming` may be refused, and the copy fallback should then work), vibration (hfd-service), the CPU boost (`sched_setattr` with uclamp on the app's own threads), and the JIT's RWX and `mprotect` use.

   Gamepads: `/dev/input/event*` is in no policy group. Options: the `unconfined` template (OpenStore allows it with a justification; most of the limits above would then not apply), or a narrower custom rule. Decide from what the confined tests show.
3. Desktop file and hooks:
   - `X-Lomiri-Touch=true`. A click runs as one instance by default, so it does not need `X-Lomiri-Single-Instance`, which the development entry does (3.7).
   - A `content-hub` hook with `{"destination": ["all"]}`, as `dev-install.sh` writes for the development entry.
   - A launcher that sets what `ut/phone-debug/launch-dolphin.sh` sets: `QT_QPA_PLATFORM=wayland`, `QT_PLUGIN_PATH` for the bundled xdg-shell plugin, `QT_WAYLAND_DISABLE_WINDOWDECORATION=1`, and `PULSE_PROP="media.role=multimedia"` (stage 2: otherwise the volume keys do not apply). The TLS variables come from the session.
   - Nothing else to configure: the user folder follows `APP_ID` (3.7), and the defaults are written at the first start (3.8), so no `Dolphin.ini` is shipped.
   - A lifecycle exemption only if one turns out to be needed (Lomiri stops an app in the background; the game pauses itself first).
   - The click's user folder is a new, empty one (`~/.local/share/<package>`). Remove the development entry (`dev-install.sh remove`) once the click works, so the drawer has one Dolphin.
4. GPL compliance: link to the exact source commit of this fork in the package and on the OpenStore page.
5. OpenStore submission, after testing on a second Ubuntu Touch 24.04 device if one is available. Other devices will have other Adreno or Mali blobs and other libhybris builds.

Exit: the click installs from a file on a clean user account, and games run with no manual setup.

#### Status (2026-11-16): done (on-screen tests and the fullscreen re-test passed); the OpenStore submission waits on the user's decisions

Notes: `stage5-packaging.md`.
- **The click** (`ut/click/`, `build-click.sh`, `install-click.sh`): `dolphin-ut.alphaliu01` (provisional name), framework `ubuntu-touch-24.04-2.x`, 13.7 MB. It holds the binary, `Sys/`, the launcher, the hooks and the bundled Qt 6 xdg-shell plugin. The cross container now has `click` and the plugin. Installed through the click service on the system bus (24.04 has no `pkcon`), or from the File Manager through OpenStore.
- **Confined, it works except gamepads:** Vulkan, dual core and the CPU boost (Need for Speed at 100 %), audio, covers, keep-display-on, Wii. It needed:
  - a launcher with shell builtins only;
  - absolute Wayland and PulseAudio socket paths (Lomiri gives clicks `XDG_RUNTIME_DIR=~/.cache`);
  - `memfd_create` for Dolphin's memory arena (`Common/MemArenaUnix.cpp`; `/dev/shm` names are denied).
- **Gamepads:** the confined template denies `/dev/input` and udev's data, and a click cannot override a deny. **Decided (user): the unconfined template**; OpenStore reviews it by hand. Checked: unconfined, the evdev devices are back.
- **Fullscreen through Lomiri:** Lomiri keeps one saved window state per app it manages, so the library came back fullscreen after a game, and the game came back from an app switch without the panel's 63 px (a strip of wallpaper). Both windows now correct their state (S5-M6 passed).
- Open: OpenStore submission (name, maintainer, title, screenshots, a pushed commit), and a second device.

### 6. Feature completeness (after stages 5 and 4)

- **The 3.6 second pass:** settings from the in-game menu (the settings pages are in the library window, hidden during a game), a Yes/No answer for Dolphin's alerts (today they are logged, shown and ignored), Hotkeys, RetroAchievements, and the remaining skipped settings.
- **Wii file actions** (3.7): Install WAD, Import Wii Save, Import BootMii NAND Backup, Load Wii System Menu, Online System Update. They are small now that the file picker exists.
- Export through content-hub (save states, memory cards), and the zip import/export of the user data.
- The other Android features deferred in stage 3: cheats, Riivolution, Skylanders and Infinity Base, NetPlay, GBA link, disc conversion, a TV layout, translations of the new strings.
- Wii Remote support: Bluetooth passthrough is unlikely on a Halium BlueZ stack. Emulated Wii Remotes with the touch overlay and motion from the phone's sensors (sensorfw) is the practical route.
- A newer Vulkan: see stage 4 step 6 (custom drivers).
- Feeding Ubuntu Touch findings back to #14962 (or its successor): as reports by the user on the PR, not as agent pull requests.

## Risks

1. **Qt6 on Mir 1.8's Wayland frontend** may lack protocols Qt 6.10 prefers, for xdg-shell versions, fractional scaling or decorations. If native Qt Wayland misbehaves, the fallback is ROCKNIX's NoGUI Wayland window (patch 015) for games, with the Qt UI only for menus, as ROCKNIX ships it. Stage 0 found that Qt 6 has no shell integration plugin on the phone and needs a bundled `libxdg-shell.so`. With it, Qt 6 reaches xdg-shell + Wayland-EGL on Mir's `xdg_wm_base` v1. Stage 1 step 5 settles the rest. A related point: click apps get `QT_QPA_PLATFORM=ubuntumirclient;wayland-egl;xcb`, and Dolphin must never pick `ubuntumirclient` (stage 2 step 3).
2. **The imported PR moves.** #14962 is open and still being reviewed. Known bugs at import: washed-out colors on KDE, and a splitter that cannot be dragged during a game. Its history will be rewritten before it merges, and it could be rejected or replaced. Pinning a hash and keeping the port's own fixes in separate commits keeps that manageable. If it is abandoned, this fork carries it alone.
3. **libhybris' Wayland EGL window** has quirks around swaps, resizes and buffer queues (see the GasTube crash). Dolphin recreates surfaces more often than most apps.
4. **An old Vulkan driver (1.1.128, 2022).** Dolphin supports 1.0+, but old Adreno blobs have known bugs. Android users usually avoid them with custom drivers, and adrenotools does not work under libhybris. Vulkan ended up the default anyway (faster in most tests, 3.8), with OpenGL ES as the fallback. Wind Waker hangs on it (stage 4 step 1); a custom driver route is stage 4 step 6.
5. **libhybris and glibc threads.** Dolphin creates GL contexts on several threads. libhybris needs extra static TLS in some programs (`GLIBC_TUNABLES=glibc.rtld.optional_static_tls=65536` was needed for Python in stone-ut). Watch for TLS-related `dlopen` failures.
6. **Qualcomm workarounds keyed on `OS_ANDROID`.** `DriverDetails.cpp` enables `BUG_SLOW_OPTIMAL_IMAGE_TO_BUFFER_COPY` (Vulkan) and `BUG_BROKEN_TEV_COLOR_ZERO` (GL ≥ 700, Vulkan ≥ `0x802bc000`) only on Android. Here the same blob runs, but Dolphin reports `OS_LINUX`, so they stay off. That can show up as wrong colors or slow EFB copies. The TEV entry changes the generated pixel shaders (`PixelShaderGen.cpp`), and the copy entry changes the Vulkan texture readback (`VKTexture.cpp`). They are the user's decision and the user's change (see "Rules for this fork").
7. **Confinement versus gamepads.** `/dev/input` is not reachable from the default click profile. The answer may be an unconfined click, which is a review step at the OpenStore.
8. **Performance ceiling.** A Snapdragon 4 Gen 1-class SoC with two big cores, behind two compositors. Some GameCube titles and many Wii titles will not reach full speed. That is a hardware fact to document per game, not a port bug.
9. **Rebasing on upstream.** Dolphin's master moves fast. The imported PR and the port's commits touch `GLContext.cpp`, `VKSwapChain.cpp`, `Present.cpp`, `DolphinQt/Main.cpp` and `RenderWidget.cpp`, so they will conflict now and then. Keep the port's own commits small.

## Files this port is expected to touch

| Area | Files | Who |
| --- | --- | --- |
| GL context | `Source/Core/Common/GL/GLContext.cpp`, `Source/Core/Common/GL/GLInterface/EGLWayland.{h,cpp}` | #14962 |
| Vulkan surface | `Source/Core/VideoBackends/Vulkan/VulkanLoader.h`, `VulkanContext.cpp`, `VKSwapChain.cpp`, `VulkanEntryPoints.inl` | #14962 |
| Surface size | `Source/Core/Common/WindowSystemInfo.h`, `Source/Core/VideoCommon/Present.{h,cpp}` | #14962 |
| Wayland input | `Source/Core/InputCommon/ControllerInterface/Wayland/`, `Source/Core/Common/Wayland/` | #14962 |
| Build | top-level `CMakeLists.txt`, `CMake/FindWayland.cmake`, `CMake/WaylandProtocols.cmake`, `Externals/wayland-protocols/` | #14962 |
| Qt platform | `Source/Core/DolphinQt/Main.cpp`, `MainWindow.cpp`, `RenderWidget.cpp` | #14962, plus a port commit for `ubuntumirclient` |
| NoGUI Wayland window (fallback only) | `Source/Core/DolphinNoGUI/PlatformWayland.cpp` | ported from ROCKNIX 015 if needed |
| QML frontend: library, emulation screen, touch overlay, in-game menu, mapping, settings | new `Source/Core/DolphinUT/` (target `dolphin-ut`), using `UICommon`, `Core`, `InputCommon/ControllerInterface/Touch` (`InputOverrider`); possibly a small overlay renderer in `VideoCommon` (stage 3.0 decision) | port |
| Packaging | `ut/click/` (manifest, AppArmor, desktop file, content-hub, launcher, build and install scripts), `ut/docker/Dockerfile` (click and the bundled Qt 6 Wayland plugin) | port |
| Custom GPU drivers | `Source/Core/DolphinUT/GpuDrivers.{h,cpp}`, `VulkanDriver.{h,cpp}`, `qml/GpuDriversPage.qml`; the stand-in `libvndksupport.so` in `ut/vkdriver/` (stage 4) | port |
| Memory arena | `Source/Core/Common/MemArenaUnix.cpp`: `memfd_create` before `shm_open` (stage 5; confined clicks may not create files in `/dev/shm`) | port, upstream candidate |
| Docs | this folder | port |

## Not checked yet

Updated 2026-11-16. Settled since the first version: the Qt 6 Widgets window (stage 1), Bluetooth gamepads (stage 0 M3, 3.5), wired USB controllers (stage 1: OTG works; the DualShock 4 clone is rejected by `hid-sony`), and the BETOP T6's layout (3.5: SDL3's database knows it in Xbox mode).

- ~~Frame pacing on the 120 Hz panel~~: settled in stage 4 (S4-M5). Games show the same frames at 60 Hz and 120 Hz, and the panel stays at 120 Hz.
- The full GLES extension list and Vulkan feature set on a window surface, as opposed to the pbuffer and headless probes in stone-ut.
- How Android Dolphin performs on this exact phone. crDroid is no longer installed, so there is no same-device baseline. Published results for SM4375 / Snapdragon 4 Gen 1 phones are the closest reference.
- Anything under click confinement (stage 5).
- microSD under confinement (unconfined, a game folder on a card works: S3.7-M7).

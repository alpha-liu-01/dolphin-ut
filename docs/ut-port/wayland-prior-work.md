# Existing Wayland work for Dolphin

Checked 2026-11-14, before stage 2 of `roadmap.md` started. The question: has someone already written Dolphin's Wayland render path, so that this port can reuse it instead of writing its own?

Answer: yes. Upstream pull request #14962 covers nearly all of stage 2, and ROCKNIX's patch set has a usable fallback. The port should start from #14962.

How this was checked:
- web search;
- `gh search prs --repo dolphin-emu/dolphin wayland` (about 40 results);
- `gh search code` for `vkCreateWaylandSurfaceKHR` and `GLContextEGLWayland`;
- reading the candidates' code, reviews and comments;
- test merges and test applies against this fork's master (`f9cb5f1c08`), without changing the working tree;
- a read-only look at the phone over adb.

Nothing was built or installed.

## Summary

| Candidate | State (2026-11-14) | Verdict |
| --- | --- | --- |
| Upstream PR #14962, "Add Wayland support (Linux only)", ellierocks | Open. Opened 2026-10-30, last push 2026-11-07. Under active review by Dolphin maintainers. 17 commits, 61 files, +2983 −56 | **Base for stage 2** |
| ROCKNIX `dolphin-sa` patches 011–017 | Maintained by a distribution for ARM handhelds. Pinned to Dolphin `6094cfcf7b` (release 2606). Last change 2026-08-17 | **Fallback.** Patch 015, a Wayland platform for the NoGUI frontend, is the useful part |
| Upstream PR #14652, "Add Standalone Wayland Support", BeezBumba | Open, stalled since July 2026. Its commits did not all build, and it had no Wayland keyboard or mouse | Superseded by #14962, which carries over its two useful commits |
| TellowKrinkle's `Wayland` branch | Last commit 2024-12-16 | The origin of #14652 and of #14962's Vulkan commit |
| Upstream PR #14724, webOS NoGUI Wayland/EGL plus aarch64 packaging, theromis | Closed 2026-07-12. Rejected under the Generative AI policy | Not useful. It is a variant of ROCKNIX's approach with a webOS-only shell protocol |
| Upstream PR #14758, "Forbid EGL_PLATFORM to be wayland", JoshuaVandaele | **Merged**, already in this fork | Not a Wayland path. It only touches `EGLX11.cpp`. See "Ubuntu Touch findings" |
| Older: #7452 (stenzek, 2018), #8727 (artemist, Vulkan only, 2020–22), #11257 (LaserEyess, 2022–23), soreau/dolphin-emu (2014), newhacker1746/dolphin-wayland (2019) | Closed or abandoned | Too old to reuse |
| Knulli, Moss, Arch-R, JELOS and other ROCKNIX-derived distributions | Carry `wayland` patches for the same package | Most likely the same patch line as ROCKNIX. Not checked one by one |

## Upstream PR #14962

URL: `https://github.com/dolphin-emu/dolphin/pull/14962`. Head: `ellierocks767/dolphin`, branch `wayland-upstream`, commit `5003b700ffecffde62fb8fe793921512a44feb5e` (2026-11-07). Merge base with this fork: `3eb30c7620` (2026-10-25).

### Against this fork

- Fetched into `refs/remotes/pr/14962` (`git fetch https://github.com/dolphin-emu/dolphin.git pull/14962/head:refs/remotes/pr/14962`).
- `git merge-tree --write-tree HEAD pr/14962` reports **no conflicts** against `f9cb5f1c08`, which is 106 commits newer than the PR's base.

### What it does

From the PR description, checked against the code:
- **Build.** An `ENABLE_WAYLAND` option (default `ON` on Linux) and `FindWayland.cmake`.
  - The protocol XML files are vendored in `Externals/wayland-protocols/`: content-type-v1, pointer-warp-v1, pointer-constraints-unstable-v1 and relative-pointer-unstable-v1. Only `wayland-scanner` is needed from the system.
  - Without `wayland-scanner` or xkbcommon there is no Wayland input, and Qt is forced back to xcb (`HAVE_WAYLAND_INPUT`).
- **OpenGL.** `GLContextEGLWayland`. It calls `eglGetDisplay()` on the `wl_display`, and creates the window with `wl_egl_window_create()` on the `wl_surface`. `libwayland-egl.so.1` is loaded at run time.
  - Resizes go through `wl_egl_window_resize()`.
  - The existing EGL code picks the GL or GLES mode, so the GLES detection described in `roadmap.md` still applies.
- **Vulkan.** `VK_KHR_wayland_surface` (TellowKrinkle's commit).
  - The surface format and present mode queries retry on `VK_INCOMPLETE` instead of asserting.
- **Surface size.** A `wl_surface` cannot report its size, and Vulkan's `currentExtent` is undefined on Wayland. `WindowSystemInfo` therefore gets `render_surface_width/height`, and `Presenter` gets a requested-size field that the host updates on resize.
- **Input.** A ControllerInterface backend for the Wayland keyboard and relative mouse (xkbcommon key names), on its own event queue. It binds only `wl_seat`, plus the optional pointer protocols.
- **Qt.**
  - The `xcb` forcing in `DolphinQt/Main.cpp` is kept only for builds without Wayland input.
  - `GetWindowSystemType()` maps any platform name that starts with `wayland` (so `wayland-egl` too) to `WindowSystemType::Wayland`.
  - `Qt::AA_DontCreateNativeWidgetSiblings` is set, because every native widget is a subsurface on Wayland.
  - The render widget reports its `wl_surface`.
  - Balloon tooltips become `xdg_popup`s.
  - The desktop file name is set.
- **Hotkeys.** Global shortcuts through the desktop portal (`DolphinQt/GlobalShortcuts.cpp`, 372 lines), and a way for `HotkeyManager` to take presses from outside the controller interface.
- **VRR.** A `wp_content_type_v1` "game" hint. No `wp_tearing_control`.
- **Flatpak.** The manifest switches to Wayland when it is available.

Commits, oldest first:

| Commit | Author | Subject |
| --- | --- | --- |
| `0b4906c41a` | ellierocks | CMake: Add Wayland support |
| `30454d78df` | TellowKrinkle | Vulkan: Add Wayland support |
| `d9d54bc1cc` | ellierocks | VideoCommon: Report the window size with surface resizes |
| `6b018d4ed2` | ellierocks | OGL: Add Wayland support through EGL |
| `2a49907970` | ellierocks | CMake: Generate Wayland protocol bindings |
| `383917c455` | ellierocks | Common: Add a Wayland connection helper and content type hint |
| `91e2fe4cf9` | ellierocks | InputCommon: Add a Wayland keyboard and mouse backend |
| `b8b2a23855` | ellierocks | InputCommon: Let the XInput2 and Wayland keyboard and mouse stand in for each other |
| `818007f92b` | ellierocks | DolphinQt/MappingWindow: Switch to the device standing in for the configured one |
| `c9ee5dc2ab` | ellierocks | DolphinQt/RenderWidget: Use the wl_surface as the render handle on Wayland |
| `e2f2d3b68a` | ellierocks | DolphinQt/Achievements: Don't make the settings widget a native window |
| `781f2c4b03` | BeezBumba | DolphinQt: Select the Wayland platform when built with Wayland support |
| `8d4d297a78` | ellierocks | DolphinQt/BalloonTip: Make tooltips work on Wayland |
| `6246b247d1` | ellierocks | Flatpak: Use Wayland when available |
| `c0ebd6df74` | ellierocks | DolphinQt: Set the desktop file name |
| `85f81c1a9b` | ellierocks | Core/HotkeyManager: Let hotkeys be pressed from outside the controller interface |
| `5003b700ff` | ellierocks | DolphinQt: Trigger hotkeys from the desktop's global shortcuts on Wayland |

The first four commits are the render path the roadmap needs. The rest are input, Qt fixes and desktop integration. They are still worth taking, because they fix Qt-on-Wayland problems (subsurfaces, native siblings, tooltips) that Lomiri is likely to show too.

### Testing and review so far

- **Author's testing.** RTX 3070 Ti on KDE Plasma, Hyprland and Sway, with adaptive sync. Vulkan and OpenGL both run as native Wayland windows, and fullscreen, VRR, tooltips, fractional scaling and screen capture work. Not tested: other platforms, ARM, GLES, mobile compositors.
- **Problems reviewers found.**
  - "Render to Main Window" plus fullscreen closed Dolphin on Sway with `xdg_surface … the surface already has a role assigned wl_subsurface`. Fixed in `f8933f073`.
  - The splitter between the log window and the render widget could not be dragged while a game ran. Partly fixed; still broken during a game at the last report (2026-10-31).
  - Missing `wayland-protocols` once left working video with no keyboard input. Fixed: the build now falls back to xcb.
  - Colors look washed out compared with X11 (JoshuaVandaele, KDE Plasma 6.8.2, Qt 6.12.0, 2026-11-08). Open.
  - Style comments: the resize code paths are hard to follow, a helper is duplicated, designated initializers are wanted.
- **History.** The maintainers asked for a clean history (fixes squashed into the commits they fix, then force-pushed). The commit hashes above will change before any merge.

### Licence and policy

- The code is GPL-2.0-or-later, like the rest of Dolphin.
- The PR's own disclosure: apart from the TellowKrinkle and BeezBumba commits, the code was written by Claude (Sonnet 5.5) under the author's direction. Nothing in it relates to the emulated console.
- Taking it into this fork is fine under `AGENTS.md`. It is host-side code, and the commits keep their authors.
- Fixes this port needs in that code go into this fork as separate commits on top. They do not go back into the PR, since upstream does not accept agent pull requests. The user can report findings on the PR as a human.

## ROCKNIX `dolphin-sa` patches

Location: `ROCKNIX/distribution`, `projects/ROCKNIX/packages/emulators/standalone/dolphin-sa/patches/`. `package.mk` builds both frontends with `-DENABLE_WAYLAND=ON`. NoGUI with `-p wayland` plays the games, and Qt (`QT_QPA_PLATFORM=wayland`) is used only for settings.

| Patch | Lines | Content |
| --- | --- | --- |
| `011-wayland-cmake.patch` | 65 | `ENABLE_WAYLAND`, finding Wayland |
| `012-wayland-surface-dimensions.patch` | 104 | Surface size in `WindowSystemInfo` / `Presenter` |
| `013-wayland-egl.patch` | 163 | `GLContextEGLWayland`. `eglGetDisplay()` on the `wl_display`, links `wayland-egl` directly |
| `014-wayland-vulkan.patch` | 145 | `VK_KHR_wayland_surface`, surface dimensions on resize |
| `015-wayland-nogui-platform.patch` | 540 | `DolphinNoGUI/PlatformWayland.cpp`: a fullscreen `xdg_wm_base` / `xdg_toplevel` window, optional `zxdg_decoration_manager_v1`. Generates `xdg-shell` from the system's wayland-protocols |
| `016-wayland-qt-platform.patch` | 26 | Stops forcing xcb in `DolphinQt/Main.cpp` |
| `017-qt-window-device-pixel-ratio.patch` | 44 | Qt device pixel ratio fix |

Against this fork: 011–016 apply cleanly as a series (`git apply --check`). 017 fails in `DolphinQt/RenderWidget.cpp` at line 477.

What it adds that #14962 does not: the **NoGUI Wayland platform**. That is a fullscreen game window with no Qt involved. It is the fallback if Qt 6 widgets misbehave on Mir 1.8 (`roadmap.md` risk 1). It overlaps #14962 in 011–014, so the two cannot be applied together as they are. If it is ever needed, port 015 on top of #14962's surface-size and EGL code.

## Ubuntu Touch findings from this check

Read from the phone over adb on 2026-11-14.

1. **The session's Qt platform list.** Lomiri's session services run with `QT_QPA_PLATFORM=ubuntumirclient;wayland-egl;xcb` (also `QT_WAYLAND_DISABLE_WINDOWDECORATION=1`, `XDG_SESSION_TYPE=mir`). Qt 6 on the phone ships `libqpa-ubuntumirclient.so` next to `libqwayland.so`. A Qt 6 Dolphin that inherits this list would pick `ubuntumirclient`. Neither upstream Dolphin nor #14962 knows that name: upstream shows "Unknown Qt platform", and the render path falls back to Headless.
   - `liblomiri-app-launch.so.0` contains the strings `QT_QPA_PLATFORM` and `wayland`, so launched click apps may get `wayland` instead.
   - No click app was running during the check, so this was not confirmed then.
   - **Stage 0 confirmed it:** a launched click app (Calculator) also gets `ubuntumirclient;wayland-egl;xcb` (`stage0-probe.md`).
   - The port must set `QT_QPA_PLATFORM=wayland` itself (in the launcher or before `QApplication`).
2. **libhybris EGL and the Wayland display.** `libEGL_libhybris.so.0` advertises `EGL_EXT_platform_wayland`, `EGL_KHR_platform_wayland` and `EGL_KHR_platform_android`. It exports `eglGetPlatformDisplay(EXT)`, and reads `EGL_PLATFORM` / `HYBRIS_EGLPLATFORM` to load `eglplatform_<name>.so`. Both forks call `eglGetDisplay()` on a `wl_display` through glvnd. GasTube renders on this phone through the same `eglplatform_wayland.so`, but not necessarily through the same entry point. **Stage 0 confirmed it:** `eglGetDisplay(wl_display)` through glvnd loads `eglplatform_wayland.so` and gives GLES 3.2 on the Adreno, with no `EGL_PLATFORM` set.
3. **`EGL_PLATFORM`.** It was not set in any session process checked. #14758's override lives only in `GLContextEGLX11`, so it does not touch the Wayland path. If a launcher ever sets `EGL_PLATFORM`, the Wayland context needs `wayland` (or unset), never `null`.
4. **Protocols on Mir 1.8.** #14962's extras (content-type, pointer-constraints, relative-pointer, pointer-warp) are optional binds. Mir 1.8 probably lacks most of them, and the code should skip what is missing. ROCKNIX 015 requires `xdg_wm_base`. GTK 3 apps (GasTube) run on Lomiri, which suggests it is there. **Stage 0 checked the real globals:** Lomiri offers none of #14962's optional protocols, and offers `xdg_wm_base` at version 1 (plus `zxdg_shell_v6` and `wl_shell`).
5. **Global shortcuts** use the XDG desktop portal. Ubuntu Touch has no portal; the code should give up quietly. To check.
6. **Found in stage 0, not covered by either fork:**
   - libhybris reports a Vulkan `currentExtent` of 1x1 on Wayland, so #14962 would build a 1x1 swapchain;
   - Qt 6 on the phone has no xdg-shell plugin, so the click must bundle one.
   Details in `stage0-probe.md`.

## Sources

- https://github.com/dolphin-emu/dolphin/pull/14962
- https://github.com/dolphin-emu/dolphin/pull/14652
- https://github.com/dolphin-emu/dolphin/pull/14724
- https://github.com/dolphin-emu/dolphin/pull/14758
- https://github.com/dolphin-emu/dolphin/pull/7452
- https://github.com/dolphin-emu/dolphin/pull/8727
- https://github.com/ellierocks/dolphin/pull/2
- https://github.com/TellowKrinkle/dolphin/tree/Wayland
- https://github.com/ROCKNIX/distribution/tree/main/projects/ROCKNIX/packages/emulators/standalone/dolphin-sa
- https://github.com/soreau/dolphin-emu
- https://github.com/newhacker1746/dolphin-wayland
- https://wiki.gentoo.org/wiki/Dolphin_emulator

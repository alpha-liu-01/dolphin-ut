# Stage 3.8: defaults for this phone

Goal (from `stage3-roadmap.md`): a first launch that plays well without visiting the settings.

## Where each default stood

Checked in the sources before changing anything:

| Default (roadmap) | Before 3.8 | 3.8 |
| --- | --- | --- |
| Dual core (`Core.CPUThread`), decided by the user in 3.0 | Off: Dolphin turns it on only `#if defined(ANDROID)` (`MainSettings.cpp`) | **On** (port default) |
| Save states (`Core.EnableSaveStates`) | Off, as on Android | **On** (user decision after 3.7: "Save states are quite helpful") |
| Game covers (`General.UseGameCovers`), decided by the user in 3.1 | Off: on only `#ifdef ANDROID` (`UISettings.cpp`); the dev folder had it on because it was turned on by hand | **On** (port default) |
| CPU boost on, at maximum | Already: `[UbuntuTouch] CPUBoost` defaults to 1024 (the port's own setting, 3.6) | Unchanged. Stage 4 may settle another value |
| Graphics backend Vulkan | Already: Vulkan is first in this build's backend list (`VideoBackendBase::GetAvailableBackends`; OpenGL comes first only on Android) | Unchanged, plus the **OpenGL ES fallback** below |
| FPS display off | Already: `GFX_SHOW_FPS` is off by default | Unchanged |
| No Confirm on Stop | Not used: the port's Exit never asks | Nothing to set |
| `RenderToMain` | Not used: no main window to render into | Nothing to set |
| Wii Remote 1's extension | Nunchuk: `Wiimote::LoadDefaults` selects it on every platform, Android included (the `#ifdef ANDROID` there only maps rumble and motion to the phone's sensors) | Unchanged. The roadmap's "none (Android)" was wrong, so there is no decision to make |
| Launcher environment | `launch-dolphin.sh` (3.7) sets the Qt 6 plugin path, `QT_WAYLAND_DISABLE_WINDOWDECORATION=1` and `PULSE_PROP` | The click's launcher in stage 5 takes it over |

Settings that change emulated behavior (speed hacks, CPU clock override, accuracy options) stay upstream's, as the roadmap says.

## How the port sets its defaults (`PortDefaults.{h,cpp}`)

Upstream switches defaults with `#if defined(ANDROID)` in Core. The port leaves Core alone and writes its defaults from the frontend instead:
- `PortDefaults::Apply()` runs at start, after `UICommon::Init` loads the user's config.
- Each default is written into the user's Dolphin.ini **only if the key is not there**. A value the user chose, in Settings or in an older folder, is never changed.
- Each default has a version. `[UbuntuTouch] DefaultsVersion` records the last one applied, so the defaults are applied once per user folder, and a default added later (version 2) reaches existing folders once.
- The log lists what it set (`default set: Core.CPUThread = True (dual core)`).
- `-C` on the command line still wins for a run, as before (the command-line layer is above the user's).
- Per-game settings and Dolphin's game INIs still override the global value for a game, as upstream intends.

Why write into Dolphin.ini rather than keep the defaults in memory: Dolphin has no config layer below the user's, and the settings screens show and change the user's layer. Written once, the values show up in Settings as they are. The settings screens have no "reset to default" for switches, so nothing falls back to Dolphin's desktop default.

Existing user folders get the defaults where they have no value of their own:
- the dev folder (`~/.local/share/dolphin-ut`) gets dual core; save states and covers are already on there;
- the run-dolphin.sh test folder (`dolphin-ut-user-w`) gets dual core if its Dolphin.ini has no `CPUThread` line.

## Vulkan, with OpenGL ES as the fallback (`Emulator`)

If Vulkan fails to start, the game starts again on OpenGL ES at once, instead of returning to the library with an error:
- Dolphin reports a backend failure as the alert "Failed to initialize video backend!" (`Core::EmuThread`; not translated). The message alert handler passes each alert to `Emulator::NoteAlert`, which notes this one.
- When the core goes back to idle without having run, and the backend was Vulkan, the emulator logs `Vulkan failed to start: retrying with OpenGL ES for this run` and starts the same game with `OGL` for this run only.
- Once it runs, a yellow on-screen message says "Vulkan failed to start: this game runs on OpenGL ES."
- The configured backend is not changed, so the next start tries Vulkan again (a driver problem may be temporary). Settings → Graphics (global or per game) can choose OpenGL ES for good.
- Games that run better on OpenGL ES (Need for Speed in the 3.0 benchmark) are stage 4's list: per-game settings already cover them.

## Checked over adb

- **Fresh user folder** (`DOLPHIN_UT_USER=/home/phablet/dolphin-ut-user-fresh`, `run-dolphin.sh ut-start`, Need for Speed):
  - the log lists the three defaults;
  - Dolphin.ini has `CPUThread = True`, `EnableSaveStates = True`, `UseGameCovers = True` and `DefaultsVersion = 1`;
  - the game ran on Vulkan in dual core (`JITARM64 DC | Vulkan`), at 100 % speed with the CPU boost at 1024.
- **Fallback.** Vulkan cannot be switched off with `VK_ICD_FILENAMES` on this phone: there is no ICD list, as libhybris loads the driver directly. An empty `libvulkan.so.1` placed first in `LD_LIBRARY_PATH` made it fail:
  - "Failed to load Vulkan library", then "Failed to initialize video backend!";
  - the retry started 0.1 s later and ran on OpenGL ES (`JITARM64 DC | OpenGL ES`) at 100 % speed;
  - `GFXBackend` stayed unset in Dolphin.ini.
- **The user's values are kept.** With `CPUThread = False` set and `DefaultsVersion` removed, the next start set only the save states, and kept dual core off. The start after that set nothing.

## Manual tests

Screen on, unlocked.

**S3.8-M1. A first launch with the defaults.** Open Dolphin (dev) from the app drawer. Its folder had no `CPUThread` yet, so this start turns dual core on.
- Settings → Config → General: are **Dual Core (speedhack)** and **Enable Savestates** on?
- Start Newer SMB and play a minute, then open the in-game menu: are Quick Save, Quick Load, Save State and Load State there?
- Does it play at least as smoothly as before? The 3.7 runs of this folder were single core; Need for Speed varied between 19 and 100 % of full speed there (launch log), and held 100 % in dual core over adb.

Yes, everything works as expected. 

**S3.8-M2. The OpenGL ES fallback on screen.** Run this from an interactive `adb shell`, which hides Vulkan from Dolphin for this run only:

```
mkdir -p /home/phablet/dolphin-ut-fakevk && : > /home/phablet/dolphin-ut-fakevk/libvulkan.so.1
LD_LIBRARY_PATH=/home/phablet/dolphin-ut-fakevk sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut-start 60
```

- Does the game start by itself, after a short flash? 
- Is the yellow message "Vulkan failed to start: this game runs on OpenGL ES." shown in the game? The red "Failed to load Vulkan library" messages from the failed attempt may show too.

Yes, I saw exactly those.

Remove the fake library afterwards: `rm -r /home/phablet/dolphin-ut-fakevk`.

Send the answers, and the log if something fails (`/home/phablet/dolphin-ut-launch.log` for M1, `/home/phablet/dolphin-ut-user-w/run-ut-start.log` for M2).

## Results (2026-11-16)

**Both tests passed** (user):
- M1: a first launch from the app drawer had dual core and save states on, and the save state items were back in the in-game menu.
- M2: with Vulkan hidden, the game started by itself on OpenGL ES and showed the yellow note.

**3.8 done.** With it, stage 3 is closed (`stage3-roadmap.md`, "Stage 3 closed").

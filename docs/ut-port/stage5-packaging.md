# Stage 5: packaging

Goal (from `roadmap.md`): an installable click package.

## What the phone expects

Checked on the phone:
- **Framework** `ubuntu-touch-24.04-2.x` (listed in `/usr/share/click/frameworks/`). Qt 6.10 comes with 2.x, so the click targets it.
- **AppArmor** policy version 2404.2 (`/usr/share/apparmor/easyprof/policygroups/ubuntu/2404.2/`). Without a `template`, a click gets `ubuntu-sdk`, which already allows vibration (`com.lomiri.hfd`).
- **Installing:** 24.04 has no `pkcon`. clickable installs through the click service on the system bus (`gdbus call --system --dest com.lomiri.click … com.lomiri.click.Install FILE`), and so does `ut/click/install-click.sh`. A user installs a downloaded click from the File Manager: OpenStore is a content-hub destination for any file.
- Your other clicks use the namespace `alphaliu01` (`gastube.alphaliu01`, `rayut.alphaliu01`). rayut ships with `"template": "unconfined"`.

## The package (`ut/click/`)

| File | What |
| --- | --- |
| `build-click.sh` | Builds `build-ut/<name>_<version>_arm64.click` from the cross build, in the cross container |
| `install-click.sh` | Installs the newest click over adb |
| `manifest.json.in` | Name, title "Dolphin", version, framework, hooks. The description says it is an unofficial port and names the exact source (GPL): the `origin` remote and commit, and "with uncommitted changes" for a dirty build |
| `dolphin-ut.apparmor` | `"template": "unconfined"` (user decision, below), and the policy groups a confined build needs |
| `dolphin-ut.desktop` | `Exec=launcher.sh`, `X-Lomiri-Touch=true`. One instance: a click is single-instance by default |
| `dolphin-ut-content.json` | content-hub destination for "all" (Import Games, Share), as `dev-install.sh` registers for the development entry |
| `launcher.sh` | The environment `launch-dolphin.sh` sets, plus the two confinement fixes below |

What goes in (13.7 MB packed, 40 MB installed):
- `dolphin-ut`, stripped;
- Dolphin's `Sys/`, next to the binary, where Dolphin looks first. `Languages/` is left out: only DolphinQt loads it;
- the icon (`Data/dolphin-emu.svg`);
- the Qt 6 Wayland shell integration the phone lacks (stage 0): `libxdg-shell.so` and `libQt6WlShellIntegration.so.6`. They come from UBports' `qt6-wayland` and `libqt6wlshellintegration6` (arm64, 6.10.2), which the cross container now unpacks into `/opt/ut-bundle`. The container also installs `click` from UBports (`ut/docker/Dockerfile`).

Everything else is on the phone already (stage 1). Lomiri adds the click's `lib/aarch64-linux-gnu` to `LD_LIBRARY_PATH` itself.

Settings:
- **Package name: `dolphin-ut.alphaliu01`**, provisional, after your namespace. It names the app's folders (`~/.local/share/dolphin-ut.alphaliu01`, …), so changing it later starts with empty user data. `CLICK_NAME` overrides it.
- **Maintainer:** taken from `git config user.name` / `user.email` when the click is built, so no address is stored in the repository. `CLICK_MAINTAINER` overrides it.
- **Version:** Dolphin's (`2609-265` becomes `2609.265`). `CLICK_VERSION` overrides it. A changed `.apparmor` file needs a new version: reinstalling the same version keeps the old AppArmor profile (found here, below).

Usage: `ut/build.sh && ut/click/build-click.sh && ut/click/install-click.sh`.

## What confinement changed

Each problem below was found by installing the click and launching it with `lomiri-app-launch`, then reading the AppArmor denials in `dmesg`.

1. **The launcher may not run other programs.** `readlink`, `dirname`, `mkdir` and `mv` were all denied. `launcher.sh` now uses shell builtins only (`${0%/*}` for its folder). It cannot create a log folder or rotate a log, so the output goes to the session journal, which keeps every run: `sh ut/phone-debug/click-log.sh` on the phone (`-f` follows, `--all` lists every run).
2. **Wayland socket.** A confined app gets `XDG_RUNTIME_DIR=/home/phablet/.cache` (`LOMIRI_APPLICATION_ISOLATION=1`), so Qt looked for `~/.cache/wayland-0`: "Failed to create wl_display". The socket is `/run/user/32011/wayland-0`, which the profile allows (`owner /run/user/*/wayland-[0-9]* rw`). The launcher passes the absolute path in `WAYLAND_DISPLAY`, taking the folder from `DBUS_SESSION_BUS_ADDRESS` (`unix:path=/run/user/32011/bus`).
3. **PulseAudio**, the same way: libpulse looks for `$XDG_RUNTIME_DIR/pulse/native`. The launcher sets `PULSE_SERVER=unix:/run/user/32011/pulse/native`, which the `audio` group allows.
4. **Emulated memory.** Dolphin crashed when the core started ("terminate called without an active exception"), with no denial in the log: the kernel had suppressed some audit lines. A test under the profile showed it: creating `/dev/shm/dolphin-emu.<pid>` is denied, because the profile allows only `owner /{dev,run}/shm/shmfd-*`. Dolphin's memory arena (`Common/MemArenaUnix.cpp`) creates that file with `shm_open`. It now uses `memfd_create` on Linux, an anonymous memory file with no name in `/dev/shm`, and falls back to `shm_open`.
   - This is a port change in Common: host memory, not emulated behavior.
   - It should go in its own commit. It is a candidate for upstream, which you would submit, as `Contributing.md` requires.

Harmless denials, left alone: Dolphin's CPU detection (`/sys/firmware/devicetree/base/model`, `midr_el1`), the Adreno driver's `trace_marker`, Qt's `/proc/<pid>/loginuid`, and a Libertine `list` call made by Lomiri's libraries at start.

## Checked confined (the `ubuntu-sdk` template with the policy groups)

`ut/phone-debug/run-click.sh` runs the installed click under its own profile with Lomiri's environment (`aa-exec`), so a test can pass `--start`:
- **Need for Speed** (from `~/Documents/Dolphin/Games`): Vulkan, dual core, 100 % speed. The CPU boost applied to 40 threads (uclamp works confined), and keep-display-on got a cookie.
- **Audio:** the stream "Dolphin Emulator", role `multimedia`, plays (`pactl list sink-inputs`), so the volume keys apply.
- **Newer SMB** (Wii, its NAND and SD card in the click's folder): 100 % speed.
- **Covers** downloaded from GameTDB (`networking`).
- **Defaults** (3.8) written into the click's new user folder; the library found the games folder.
- **Gamepads: no.** The input devices lost every `evdev` device, the touchscreen included. The `ubuntu-sdk` template has `audit deny /dev/input/** rw` and `deny /run/udev/data/** r`. In AppArmor a deny beats any allow, so a click's extra paths (`read_path`, `write_path`) cannot open them. Only the `unconfined` template, or a full custom profile, can.

## Decision: unconfined (user, 2026-11-16)

| Option | Gamepads | OpenStore review |
| --- | --- | --- |
| **Unconfined template** (chosen) | Yes | Manual, with gamepads as the justification (like rayut) |
| Confined `ubuntu-sdk` + policy groups | No (touch only) | Automatic |
| A custom `apparmor-profile` hook | Yes | Manual, and the port maintains a ~750-line profile |

Checked: the unconfined build (`2609.265.1`) runs as `(unconfined)`, and its input devices include `evdev/0/fts_ts` and `evdev/0/uinput-goodix` again.

The confined fixes stay in place:
- the launcher's two paths are needed either way, because Lomiri sets `XDG_RUNTIME_DIR=~/.cache` for every click;
- `memfd_create` keeps a confined build possible: `CLICK_CONFINED=1 ut/click/build-click.sh`.

The policy groups stay in the `.apparmor` file. They have no effect with the unconfined template, but they document what a confined build needs.

## Not done in this stage

- **OpenStore submission** (step 5). It needs, from you:
  - the final package name and maintainer;
  - the title: "Dolphin" now, with "unofficial port" in the description. Whether to say "unofficial" in the title too is your call, since Dolphin is the Dolphin team's name;
  - screenshots, the license (GPL-2.0-or-later), and the unconfined justification;
  - a commit pushed to GitHub, so the source link in the manifest works.

  A release build should come from a clean, committed tree: the version and title string say "dirty" otherwise.
- A second device (step 5): none available.
- **The development entry:** it stays until you remove it (`sh /home/phablet/dolphin-ut-probe/dev-install.sh remove`). It has its own user folder (`~/.local/share/dolphin-ut`), and the click starts with a new one (`~/.local/share/dolphin-ut.alphaliu01`). The games in ~/Documents/Dolphin/Games are shared. Both appear when you share a file from the File Manager.

## Manual tests

Screen on, unlocked. Installed now: **Dolphin** `2609.265.1` (the click, unconfined), next to "Dolphin (dev)". If something fails, send the output of `sh /home/phablet/dolphin-ut-probe/click-log.sh` (an `adb shell` on the phone).

**S5-M1. Install from a file, as a user would.**
- File Manager → Downloads → `dolphin-ut.alphaliu01_2609.265.2_arm64.click` → Share or Open with → OpenStore → install.
- Does it install as an update (2609.265.2), and does "Dolphin" still open with the same library and settings?

Yes, everything works as expected. 

**S5-M2. The app.**
- Open **Dolphin** (not "Dolphin (dev)") from the app drawer: the Dolphin icon, the library with covers.
- Settings → Config → User Data: is the user data folder `~/.local/share/dolphin-ut.alphaliu01/`, with the app ID `dolphin-ut.alphaliu01_dolphin-ut_2609.265.2`?

Yes, everything works as expected. 


**S5-M3. Play.** Start Need for Speed and Newer SMB from the library, with touch:
- Is there sound, and do the volume keys change it?
- Does the touch overlay vibrate?
- In the in-game menu: Take Screenshot (in the Gallery?), Save State, then Load State.
- Switch to another app and back (paused and resumed), then Exit.

Yes, everything works as expected. 


**S5-M4. Gamepad.** Pair the BETOP T6, start a game: does the pad play it (automatic mapping as in 3.5)?

Yes, everything works as expected. 


**S5-M5. Import and share.**
- Library ⋮ → Import Games → File Manager → pick a game: imported?
- File Manager → Share a game → **Dolphin** (the click): imported into the click?

Yes, everything works as expected. 


But I did notice something wrong. This time, the game library is not fullscreen on launching the Dolphin app (which is normal). I entered a game and exited it, the game library page became fullscreen, which is not supposed to happen. 

When I switched to a different app from Dolphin gameplay and back, the fullscreen state of the game was broken, with a stripe of wallpaper BELOW the picture, which is about as tall as the top bar. Rotate the phone (to portrait or landscape) removed that wallpaper stripe. 

## Results (2026-11-16)

**All five tests passed** (user):
- M1: installed from the File Manager through OpenStore as an update, keeping the library and settings;
- M2: the app folders and app ID;
- M3: play with touch: sound and volume keys, vibration, screenshot, save states, app switch, exit;
- M4: the gamepad (automatic mapping);
- M5: Import Games and Share.

The user reported two fullscreen problems:
1. **The library came back fullscreen after a game.** At launch it is a normal window, as it should be.
2. **After switching to another app and back during a game**, the picture no longer filled the screen: a strip of wallpaper below it, about as tall as the top bar. Rotating the phone fixed it.

### Cause: Lomiri keeps one window state per app

The game's own log, in the journal, showed problem 2. Back in the app, Lomiri resized the game window to **2400x1017**: the landscape screen minus the 63 px top panel. It kept the panel hidden, so the 63 px showed the wallpaper. A rotation gave 2400x1080 again.

Lomiri's shell explains why this appeared only now:
- It saves the window state of an app when the app's window closes, and applies it to the app's next window (`/usr/share/lomiri/Stage/WindowStateSaver.qml`, `Stage.qml`: `WindowStateStorage.saveState(appId, …)`, then "Saved window state is fullscreen, using initial window state").
- It does this only for apps it manages, ones started from the drawer or by content-hub. In 3.2 the app ran from `run-dolphin.sh` over adb, which Lomiri does not manage ("AppManager is not managing"), so the library came back normal then.
- The fullscreen game window is the last window to close before the library shows again, so the library inherits "fullscreen".

Checked through Lomiri (the dev entry, with test arguments; below): right after a game exits, Lomiri sends the library window the fullscreen state (`library window: fullscreen` in the log).

### Fixes

- **Library** (`Emulator`): when the library window, shown again with no game running, is told it is fullscreen, it asks for a normal window (`showNormal`: `xdg_toplevel.unset_fullscreen`). Checked through Lomiri: it ends normal. Lomiri sent fullscreen a second time, and the second request settled it. At most three requests each time the library comes back.
- **Game** (`GameWindow::CheckFullscreen`): whenever the game window is active and its size has been stable for 0.5 s, it compares that size with the screen. If the window does not cover the screen, it asks for fullscreen again (unset, then set), at most three times. The count resets each time the app becomes active. The log says `game window WxH does not cover the screen (…): asking for fullscreen again (n)`.
  - This could not be confirmed over adb: the phone was locked during the runs (`com.lomiri.LomiriGreeter.IsActive` true), and Lomiri does not fully lay out apps behind the lock screen. The re-test below checks it.
- New log lines: `library window: …` and `game window: …` (Qt's window state), and `library window size`.

Test tools added:
- `--stop-after MS` (exit the game to the library, as the in-game menu's Exit does);
- `/home/phablet/dolphin-ut-launch.args`: the dev entry (`launch-dolphin.sh`) reads its arguments once from this file, so a Lomiri-managed start can boot a game, for example `--start "<game>" --stop-after 30000`. Lomiri-managed starts behave differently from adb runs, as above.

### Re-test

Installed: **Dolphin** `2609.265.3`.

**S5-M6. Fullscreen through Lomiri.** Open Dolphin from the app drawer, start Need for Speed.
- Switch to another app and back, a few times, holding the phone in landscape: does the picture fill the screen each time (no wallpaper strip)?
- Exit to the library: is it a normal window with the top panel?
- Start a game again: fullscreen?

If not, send `sh /home/phablet/dolphin-ut-probe/click-log.sh | grep -E "window|application state"`.

**Passed (user, 2026-11-16): "the fix works".** Stage 5 is done, except the OpenStore submission, which waits on the decisions listed in "Not done in this stage".

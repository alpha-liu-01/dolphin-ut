# Stage 6.2: the remaining settings

Goal (`stage6-roadmap.md`): close the settings generator's "skipped" list. Six of its items are covered here:
- Hotkeys;
- Post-Processing Effect;
- the log settings (verbosity, log types, the three log actions);
- Set Custom RTC;
- the two JIT profiling actions.

Status (2026-11-16): **done.** Checked over adb, and all on-screen tests (S6.2-M1–M5) passed.

## What was skipped, and what is left

`settings.json`'s "skipped" list went from 19 entries to 9. The 9 left are deliberate:

| Left out | Why |
| --- | --- |
| Generate a New Statistics Identity | Analytics are not built into the port (`ENABLE_ANALYTICS=OFF`) |
| Change App Theme (×2), Change Theme Mode, Use Black Backgrounds | The Android app's own Material theme; the port follows Lomiri's |
| Enable Achievements, Log In, Log Out | Stage 6.7 (RetroAchievements) |
| Real Balance Board | No real Wii Remotes on the phone (stage 3.5) |

## How it works

All of it goes through Dolphin's existing code, as the Android app (and DolphinQt for the log) does. Nothing changes how the emulated consoles behave.

### The generator (`ut/tools/gen-settings.py`)

New item kinds, read from the Android presenter as before:

- **Choices whose list is built at run time** carry `"dynamic"`, and `SettingsModel::choices` builds the list:
  - **Post-Processing Effect** (`postProcessingShaders`): "Off", then `VideoCommon::PostProcessing::GetShaderList()`, or the anaglyph list in anaglyph stereo mode, as the presenter's `shaderList`. 47 shaders ship in the click's `Sys/Shaders`.
  - **Verbosity** (`logVerbosity`): the Android list that ends in Debug, cut to the build's highest level (`Common::Log::MAX_EFFECTIVE_LOGLEVEL`), as `getLogVerbosityEntries` picks a list. A Release build shows Notice, Error, Warning, Info.
- **`datetime`**, for Set Custom RTC, keeps the Android picker's range: 2000-01-01 to 2035-12-31.
- **`logTypes`**, for the log type switches: `SettingsModel::menu` turns it into one switch per Dolphin log type (`LogManager::GetLogTypes`, 58 of them), titled with the full name, as `NativeLibrary.GetLogTypeNames`.
- **New actions:**
  - `log_enable_all`, `log_disable_all`, `log_clear`;
  - `jit_wipe_profiling`, `jit_write_block_log`.
- **Hotkeys** now point to a page (`HotkeysPage.qml`) instead of being hidden. The page is global only, as on Android, which shows it only outside game settings.
- **The Log menu** is global only: Dolphin reads the Logger file globally, not per game.

### Hotkeys

- **The page** (`HotkeysPage.qml`) is the mapping page from stage 3.5, over Dolphin's hotkey controller (`HotkeyManagerEmu`, `Hotkeys.ini`). It has the device, "Map all devices", Reset to Default, Clear, Profiles, and tap-to-detect or press-and-hold to edit an expression.
- **The hotkeys listed** are the ones the Android app handles (`HotkeyScheduler::GetSupportedHotkeys`), under a group header each time the group changes, as `addHotkeySettings`. That is 56 hotkeys:
  - screenshot;
  - speed limit off, slower, faster;
  - volume down, up, mute;
  - internal resolution up and down;
  - selected-slot save, load, next and previous; first-slot save; undo load and undo save;
  - save, load, select and load-last for slots 1–10.
  - `MappingController::ShowOnly` filters the controls; the QML is unchanged.
- **Handling them:** `DolphinUT/HotkeyScheduler.cpp` is the Android app's `Source/Android/jni/HotkeyScheduler.cpp` without its JNI entry point.
  - A thread polls every 5 ms while the game runs.
  - It runs only while at least one listed hotkey is mapped, as Android's battery fix does.
  - `Emulator` starts it when the game leaves Starting and stops it at Stopping, as `MainAndroid`'s `Run` does.
  - Hotkeys are off while the library window is over the game (settings, a question) and while the app is not active, as Android's `Hotkeys.setEnabled` in `onResume`/`onPause`.
  - Mapping a hotkey wakes the thread (`RefreshActiveHotkeys` on save), also during a game.
- **None mapped at first.** Without a `Hotkeys.ini`, Dolphin loads desktop Dolphin's defaults: keyboard keys, for example F1–F8 to load states. That would keep the thread polling in every game on a phone without a keyboard; 1.4 % of a core was measured.
  - The Android app maps nothing by default (`HotkeyManager::LoadDefaults` is `#ifndef ANDROID`).
  - So on its first start the port writes an empty mapping (`Controllers::Start`; logged "hotkeys: none mapped (first start)").
  - **Reset to Default** on the hotkey page still gives Dolphin's keyboard keys, on Dolphin's default device (the keyboard: `Wayland/0/Keyboard Mouse`). The dialog says so.
- **Input detection with the game paused** (a fix to stage 3.5 and 6.1): the mapping page polled the devices itself only when no game existed. With Settings over a paused game (6.1), nothing polled them, so detection could never see a press. It now polls whenever the game is not running.

### The log, and a stage 3.6 bug it fixed

- **The bug:** Dolphin's `LogManager` reads `Logger.ini` once, at start, and writes its own state back at exit (`LogManager::Shutdown` → `SaveSettings`). Since 3.6, the "Write Logs to File" switch changed only the config. So it had no effect until the next start, and the exit then put the old value back: it never took effect.
- **The fix:** the log settings now also go to the `LogManager`, as DolphinQt's `LogConfigWidget` does:
  - `EnableListener(FILE_LISTENER)` for "Write Logs to File";
  - `SetEnable(type)` for each log type, including Enable/Disable all.
  - The Android app instead re-creates the `LogManager` (`ReloadLoggerConfig`), which can race threads that are logging.
- **Verbosity** needed no change: the `LogManager` follows it through a config callback.
- **Clear Log file** empties `Logs/dolphin.log` rather than deleting it, as `SettingsAdapter.clearLog`. The log is written in append mode, so it carries on.

### Set Custom RTC

The value is Dolphin's `Core.CustomRTCValue`, in seconds since 1970. It is shown and picked as UTC wall-clock time, as the Android app's `DateTimeSettingViewHolder` and `onDateTimeClick` do. The dialog has:
- a Lomiri date picker (year, month, day);
- a time picker (hours, minutes; seconds are 0, as on Android).

Both RTC settings are locked while a game runs (Android's `NOT_RUNTIME_EDITABLE`, from 6.1).

### The JIT profiling actions

`NativeLibrary.WipeJitBlockProfilingData` and `WriteJitBlockLogDump` do the same thing here: under a `CPUThreadGuard`, on the UI thread as on Android.
- **Without a game:** "JIT is not active".
- **Write Jit Block Log Dump** writes `Dump/Debug/JitBlocks/<game ID>.txt` and shows "Wrote to …".
- **Profiling numbers:** Dolphin fills a block's run count and times only when Dolphin's debug mode is also on (`JitBase::IsProfilingEnabled`: `Interface.DebugModeEnabled`). Neither the Android app nor the port has a switch for it, so on both the dump lists blocks with "-" for their numbers. With `-C Dolphin.Interface.DebugModeEnabled=True` the numbers are there (checked below). That is Dolphin's behavior; the port changes nothing.

### Action results

`SettingsModel::actionFinished` now carries a message, and the result dialog shows it ("JIT is not active", "Wrote to …"). The log and JIT actions run on the UI thread, as on Android. The finish signal is queued, so the page is waiting for it when it comes. The SD card actions stay on a worker thread.

### Test aids (adb)

- **`main.cpp --test-action ACTION@MS`** (repeatable) runs a settings action during a game.
- **`page-harness.qml`:**
  - `hotkeys`;
  - `hotkeys-map --test-arg DEVICE`: maps Take Screenshot and Volume Up to a Pipes device's Button A and B;
  - `remaining-roundtrip`;
  - `log-sync`;
  - `settings-tap:MENU --test-arg "ROW TITLE"`: opens a menu with that row tapped, and logs whether the dialog scrolls.

## Checked over adb

On the stone, with the test build and the test user folder:

| Check | Result |
| --- | --- |
| Shader list | "Off" + 47 shaders (16bit, 32bit, AutoHDR, FXAA, …) |
| Verbosity list (Release build) | Notice, Error, Warning, Info |
| Log menu | 6 items (with the Log Types header) + 58 log type switches |
| RTC round trip | default 946684800 (2000-01-01); set 1234567800, read back; shown as 2009-02-13 23:30 |
| Enable all / Disable all / Clear log | done; switches follow (VI, SI true/false) |
| JIT actions without a game | "JIT is not active" for both |
| Log changes survive the exit | all types off but Boot, file log on, verbosity 4: still so in `Logger.ini` after the app quit (the `LogManager` wrote back the same values) |
| File log live | with "Write Logs to File" on, `Logs/dolphin.log` fills during the game (BOOT lines) |
| Hotkeys mapped through the app | `Hotkeys.ini`: `Device = Pipe/0/hk`, `General/Take Screenshot = `Button A``, `Volume/Volume Up = `Button B`` |
| Hotkeys during a game (Need for Speed) | Button A pressed through the FIFO: a new screenshot in `Pictures/Dolphin/GOWP69/`. Button B: volume up ran (already 100 %, stored) |
| Hotkey thread cost | mapped: 14–17 ticks in 10 s (1.4–1.7 % of a core); nothing mapped: 0 ticks |
| First start | "hotkeys: none mapped (first start)"; `Hotkeys.ini` with only the device |
| JIT actions during a game | wipe: done; dump: "Wrote to …/Dump/Debug/JitBlocks/GOWP69.txt", 19081 blocks; with debug mode on, 10702 blocks with run counts |
| Post-processing on Turnip (16bit) | no errors, 100 % speed |
| Pages (screenshots) | hotkey page, log menu, RTC dialog, shader dialog: as intended; the shader dialog scrolls (content 6233 px in 2137 px) |

The test folder's `Dolphin.ini`, `Logger.ini` and `GFX.ini` were restored afterwards, and the Pipes test device removed.

## Manual tests

Screen on, unlocked, from an interactive `adb shell`. `run-dolphin.sh` runs the test build (`/home/phablet/dolphin-ut-arm64`, already deployed); the installed click does not have 6.1 or 6.2 yet.

`GAME="/home/phablet/Documents/Dolphin/Games/Need for Speed - Most Wanted.iso" sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 900`
(the library; start Need for Speed from it when a test says so)

**S6.2-M1. Hotkeys.** Pair the gamepad first (the BETOP T6, or the DualShock 4 over Bluetooth). Library ⋮ → Settings → **Hotkeys**.
- Do you see the groups (General, Emulation Speed, Volume, Internal Resolution, then the save state groups), each hotkey "Not mapped"? Yes. 
- Device: pick the `SDL/…` gamepad. Tap **Take Screenshot**, then press a button the game does not use much (the Back/Select button, for example). Is it mapped? Yes. I used the keyboard instead since the controller is already fully mapped to the controller with no spare buttons.
- Start Need for Speed and press that button in a race. Does "Screenshot saved" (or similar) show, and is there a new picture in Gallery (Pictures/Dolphin)? Yes. 
- In the game: Menu → Settings → Hotkeys, map **Volume Toggle Mute** to another button the same way, then Back to the game. Does the detection see the press with the game paused, and does the button mute and unmute the game?

Yes. But the unmute immediately turns the volume to 100%. I'm not sure if this is supposed to happen, but a bit annoying. 

**S6.2-M2. Post-Processing Effect.** During the race: Menu → Settings → Graphics Settings → Enhancements → **Post-Processing Effect**.
- Does the list scroll to the end (many shaders), with Cancel reachable? Yes. 
- Pick **grayscale** (or 16bit), then Back to the game: does the picture change? Set it back to Off. Yes.

**S6.2-M3. Custom RTC.** Library ⋮ → Settings → Config → Advanced → Custom RTC Options.
- Tap **Set Custom RTC**: do the date and time pickers work by swiping, and does OK show the chosen date and time under the title? Yes. 
- During a game (Menu → Settings → Config → Advanced): are both RTC items greyed out? Yes. 

**S6.2-M4. Log.** Library ⋮ → Settings → Config → **Log**.
- Verbosity: Notice to Info. Turn **Write Logs to File** on, then quit and start Dolphin again: is it still on? Yes. 
- Disable all Log Types: do the switches turn off? Then Enable all: on? (Leave it as you like.)

**S6.2-M5. Debug actions.** During a game: Menu → Settings → Config → Debug → **Write Jit Block Log Dump**. Does a dialog say "Wrote to …/Dump/Debug/JitBlocks/GOWP69.txt"? From the library (no game): "JIT is not active"?

Yes. 

## Results

All five on-screen tests passed (2026-11-16, test build, test user folder).

| Test | Result |
| --- | --- |
| M1 Hotkeys | Pass. The groups showed, all "Not mapped". The gamepad had no spare buttons, so the user mapped a **keyboard** instead (`Wayland/0/Keyboard Mouse`: Take Screenshot Ctrl+P, Volume Down "-", Volume Up "=", Volume Toggle Mute F4). The screenshot hotkey worked in the race (`Pictures/Dolphin/GOWP69/GOWP69_2026-11-16_20-18-29.png`). Mapping during the paused game detected the key, and mute/unmute worked. The keyboard went through Dolphin's Wayland input backend, untested before |
| M2 Post-Processing Effect | Pass: the list scrolls to Cancel; grayscale changed the picture; back to Off |
| M3 Custom RTC | Pass: the pickers work by swiping, OK shows the date and time (set to 1072915200, 2004-01-01 00:00); both items greyed out during a game |
| M4 Log | Pass: "Write Logs to File" stayed on after quitting and starting again (the 3.6 bug is fixed). Disable/Enable all was not answered; it passed over adb |
| M5 Debug actions | Pass: "Wrote to …/GOWP69.txt" during a game; "JIT is not active" without one |

### Notes on the session

- **"Unmute turns the volume to 100 %" is expected.**
  - Volume Toggle Mute is Dolphin's own (`AudioCommon::ToggleMuteVolume`): mute plays at 0, and unmute goes back to Dolphin's volume setting (`DSP.Volume`). It does not change that setting.
  - That setting was at its default, 100 % (no `Volume` key in `Dolphin.ini`), so the message after unmuting says 100 %.
  - It is Dolphin's level within the phone's volume, not the phone's volume: the sound comes back as loud as before muting.
  - After Volume Down (the "-" key, 3 % a press), unmute returns to that lower level.
  - The Android app and desktop Dolphin behave the same. No change.
- **"FrameDump: Dolphin was not compiled with FFmpeg …"** came at 20:18:29, the moment of the hotkey screenshot. Dolphin saves screenshots through its frame dumper thread, which logs this once when built without FFmpeg (the port's build has no FFmpeg). Screenshots are PNGs either way. Harmless.
- **"Delete: …/Backup/SYSCONF does not exist"** at each game stop: Dolphin deletes its backup of the Wii settings file when a game ends cleanly (`Core::RestoreWiiSettings`), and a GameCube game makes no backup. Harmless; desktop Dolphin logs it too.
- **"Failed to GET …/cover/JA/SMNW03.png: 404":** GameTDB has no Japanese cover for Newer Super Mario Bros. (a fan game); the library tries the next region. Known since 3.1.
- These lines reached the terminal because the log was now written to `Logs/dolphin.log` (M4 turned the file log on).
- **Exits:** "exited by itself after 529s (status 0)" and "after 37s" were the user quitting the app (M4's restart); the `^C` run ended through `run-dolphin.sh`'s SIGINT.

### The user's terminal output

```
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell
phablet@ubuntu-phablet:~$ GAME="/home/phablet/Documents/Dolphin/Games/Need for Speed - Most Wanted.iso" sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 900
running: ut: -u /home/phablet/dolphin-ut-user-w (for 900s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
exited by itself after 529s (status 0)
15:46:612 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
15:47:742 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
18:29:319 VideoCommon/FrameDumper.cpp:210 W[Video]: FrameDump: Dolphin was not compiled with FFmpeg, using fallback option. Frames will be saved as PNG images instead.
21:08:218 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
22:57:751 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
24:17:152 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
phablet@ubuntu-phablet:~$ cd /home/phablet/dolphin-ut-user-w
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ cp run-ut.log run-ut-2.log
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ GAME="/home/phablet/Documents/Dolphin/Games/Need for Speed - Most Wanted.iso" sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 900
running: ut: -u /home/phablet/dolphin-ut-user-w (for 900s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
^C
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ GAME="/home/phablet/Documents/Dolphin/Games/Need for Speed - Most Wanted.iso" sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 900
running: ut: -u /home/phablet/dolphin-ut-user-w (for 900s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
exited by itself after 37s (status 0)
30:46:418 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
30:47:478 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ 
```

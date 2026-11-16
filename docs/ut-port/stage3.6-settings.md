# Stage 3.6: settings

Goal (`stage3-roadmap.md`): the Android app's settings, in its menu structure; the aim is everything it exposes (user, 3.0); per-game settings required (user, 3.0).

Status (2026-11-16): built, first and most of the second pass at once. The pages were checked from screenshots, and global and per-game values were round-tripped through Dolphin's config over adb. The on-screen tests are below.

## Approach: generated from the Android app

Rather than hand-copy a first-pass subset, the menus are **generated from the Android app's sources** by `ut/tools/gen-settings.py`, as the overlay layout was in 3.3. The Android app stays the one source for menus, order, labels, descriptions, choices and ranges.

The generator reads:
- **`SettingsFragmentPresenter.kt`**: the menus (`add*Settings`) and their items: switches, inverted switches, single choices, int, float and percent sliders, text fields, submenus, headers;
- **`BooleanSetting.kt`, `IntSetting.kt`, `FloatSetting.kt`, `StringSetting.kt`**: each setting's file, section, key and default;
- **`res/values/strings.xml`, `arrays.xml`**: the text and the choice lists;
- **Dolphin's `Core/Config/*.cpp`**: its own `Config::Info` definitions.

It writes:
- **`Source/Core/DolphinUT/settings.json`**: the menu tree (191 items in 18 menus), bundled as `:/settings/settings.json`;
- **`Source/Core/DolphinUT/SettingsTable.inc`**: each setting bound to **Dolphin's own `Config::Info` of the same name** (136 of 143). Values are therefore read and written exactly as the emulator reads them, with Dolphin's defaults. The 7 others are the Android app's own definitions (two) and the port's virtual settings (below).

`SettingsModel` (C++) serves the menus and values; `qml/SettingsPage.qml` draws any menu with Lomiri components. Item types:
- headers;
- submenus;
- switches, inverted switches included;
- choices: the current value as the subtitle, and a dialog with the entries;
- sliders: integer, float and percent, with units and steps; applied when the finger lifts;
- text fields.

### Why bind to Dolphin's objects, not the Android definitions

The Android app's definitions carry **Android's defaults**. For example, `MAIN_CPU_THREAD` is `true` there because Dolphin turns dual core on by default only on Android; on desktop Linux it is `false`. Reading with Android's defaults would show settings that are not what the emulator uses.

One location also differs: Android writes Audio Buffer Size to `Dolphin.ini [DSP] AudioBufferSize`, while Dolphin reads `[Core] AudioBufferSize`. The port uses Dolphin's, so the setting actually works. The generator reports such differences.

### The port's adjustments (in the generator)

- **Left out (Android-only):**
  - screen orientation during emulation (Lomiri decides);
  - Expand to Cutout Area;
  - app theme and black backgrounds;
  - the GPU Driver page (Android's custom drivers);
  - usage statistics reporting (analytics are not built in).
- **Redirected:** GameCube Input and Wii Input open the 3.5 **Controllers** page.
- **Not yet:**
  - Hotkeys (mapping UI for `Hotkeys.ini`);
  - RetroAchievements (needs login and logout actions; Dolphin's names for those settings differ from the Android app's).
- **Virtual settings:** items the Android app builds from converter objects:

  | Setting | What it does |
  | --- | --- |
  | DSP Emulation Engine | One choice over `Core.DSPHLE` and `DSP.EnableJIT`. ARM64 lists HLE and LLE Interpreter, as the Android app's generic list; the DSP recompiler is x86-64 only |
  | Synchronize GPU Thread | Never / On Idle Skipping / Always, over `SyncOnSkipIdle` and `SyncGPU` |
  | MEM1 and MEM2 Size | In MB over the byte values |
  | CPU Core | The ARM64 list: JIT Recompiler, Cached Interpreter, Interpreter |

- **The port's own CPU Boost**, under Dual Core in Config → General:
  - Off, Medium (512) or **Maximum (1024, default)**;
  - stored as `Dolphin.ini [UbuntuTouch] CPUBoost`;
  - see "CPU Boost" below.

### Per-game settings

As the Android app's: long press a game → **Edit Game Settings** opens the same menus for that game.
- Values go to the game's own layer: `GameSettings/<ID>.ini`, loaded with `ConfigLoaders::GenerateLocalGameConfigLoader` while the page is open, as the Android app's `NativeConfig.loadGameInis`.
- Values the game sets are marked with "•".
- **A press and hold clears a game value**, so the global one applies again (the Android app's "Long press a setting to clear it.", shown at the top of game settings).
- **Clear Game Settings** in the same dialog deletes the file.
- Not while a game runs: its own layer is in use then.
- The Controllers page is left out of game settings: it edits the global setup, and per-game controller profiles are not done yet.

Writes outside game settings go to the base layer, or to the current run's when a running game overrides the setting, as the Android app's `LAYER_BASE_OR_CURRENT`. Changes are saved at once.

### CPU Boost

`CpuBoost::Apply` sets `uclamp_min` on every thread of the app (`sched_setattr`, no root needed, as measured in 3.0).
- `Emulator` applies the setting's value when the game runs, and 0 when it is paused or stops.
- It is applied again every 5 s while running, so threads started later (shader compilers) get it too.
- The value is read through Dolphin's config, so a game's own setting applies.
- Checked: Need for Speed started with "CPU boost 1024 (uclamp_min) on 35 threads" in the log, and ran at 38–47 % in single core. That matches 3.0's single core with boost (41 %); dual core becomes the default in 3.8.

### Moved here from the Developer page

- "Show FPS and speed": now Graphics → Statistics (Show FPS, Show % Speed), as in the Android app.
- "Enable Save States": now Config → General (Enable Savestates), as in the Android app.

The Developer page keeps the log and the test switches.

## Not in 3.6 (skipped items)

The generator lists what the generic page cannot show (`settings.json` → `skipped`). By kind:
- **Actions** (`RunRunnable`): User Data (3.7), Pack/Unpack SD Card, achievements Log In/Out, log type and log file actions, JIT profiling actions.
- **File and folder pickers:**
  - Default ISO, BIOS, Game Boy Player ROM, SD Card Path;
  - the Wii NAND root, Dump, Load, Resource Pack, WFS, GameCube saves and SD sync folders.

  They belong with 3.7 (files and confinement). The ISO folders are already on the Game Folders page. **Done in 3.7**, with Pack/Unpack SD Card and User Data (`stage3.7-storage.md`).
- **Others:**
  - log Verbosity (its list depends on the build);
  - the Real Balance Board switch;
  - Set Custom RTC (a date-time picker);
  - Post-Processing Effect (its list comes from the shader folder);
  - the per-log-type switches;
  - the Hotkeys mapping.

**Settings from the in-game menu:** the Android app's in-game menu has "Settings". In the port, the settings pages live in the library window, which is hidden while a game runs: hiding it is what lets Lomiri give the game fullscreen (3.2). Showing them during a game needs either that window to come back over the paused game, or the settings drawn into the picture like the menu. This is left for the second pass. Meanwhile, settings are changed from the library, and per-game ones apply at the next start.

## Manual tests

Screen on, unlocked. From an interactive `adb shell`: `sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 900`.

**S3.6-M1. Global settings.** Library ⋮ → Settings. Browse Config (General, Interface, Audio, GameCube, Wii, Advanced, Log, Debug) and Graphics (Enhancements, Hacks, Statistics, Advanced): do the pages look right, are the labels the Android app's, and do switches, choice dialogs and sliders respond? GameCube Input and Wii Input open the Controllers page.

Yes. They do respond. 

**S3.6-M2. Settings take effect.**
- Config → General: **Dual Core on**. Graphics → Statistics: **Show FPS** on.
- Start Need for Speed: the FPS shows, and the speed is higher than single core (the log's `perf:` lines say "dual core").
- Then Graphics → Enhancements → Internal Resolution 2x: does the picture get sharper (and the speed lower)? Set it back to 1x.

Yes, settings work. 2x is visibly sharper. FPS meter showed up correctly as I set them.

**S3.6-M3. Game settings.**
- Long press Need for Speed → **Edit Game Settings** → Graphics Settings → Video Backend: **OpenGL** (the title shows "Game settings: …" and the value gets a "•").
- Back to the library, tap Need for Speed: it starts with OpenGL ES (the log says `GFXBackend` / "Using OpenGL|ES"); Newer SMB still starts with Vulkan.
- Then Edit Game Settings again, press and hold Video Backend: the "•" goes and the global value shows. Or use Clear Game Settings.

They all work as expected. 

**S3.6-M4. CPU Boost.** Config → General → CPU Boost: Off; play Need for Speed for a minute and note the speed; then Maximum again. The log shows "CPU boost 0" and "CPU boost 1024". Is the difference clear on screen?

The difference is EXTREMELY clear. In NFSMW, I got ~38-50% of full speed before, and I get 82-100% of full speed after.

**S3.6-M5. Emulated CPU clock.** Config → Advanced → Override Emulated CPU Clock Speed on, Emulated CPU Clock Speed 85 % (the user's Android habit for Need for Speed). Better frame rate in races? This one could also be a game setting for Need for Speed.

This works as expected. I get a fairly stable 30-35-ish fps with VPS staying at 50 most of the time, which matches what I got on Android on the same phone.

Send the terminal output and the answers.


phablet@ubuntu-phablet:~/dolphin-ut-user-w$ sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 900
running: ut: -u /home/phablet/dolphin-ut-user-w (for 900s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
exited by itself after 469s (status 133)
crashed (signal 5); crash data:
no register dump in the log
  core dump: -rw------- 1 phablet phablet 70397370368 Nov 16 00:31 core
23:44:735 Common/HttpRequest.cpp:283 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: Connection timed out after 3002 milliseconds
26:27:112 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/StateSaves/GOWP69.s02.dtm does not exist
26:42:544 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
27:20:796 VideoBackends/OGL/OGLConfig.cpp:744 W[Video]: Missing OGL Extensions: PrimitiveRestart PinnedMemory GSInstancing ClipControl DepthClamp 
27:20:989 VideoBackends/OGL/OGLConfig.cpp:744 W[Video]: Missing OGL Extensions: PrimitiveRestart PinnedMemory GSInstancing ClipControl DepthClamp 
28:08:479 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
30:32:259 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
30:38:174 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
31:21:231 VideoBackends/OGL/OGLConfig.cpp:744 W[Video]: Missing OGL Extensions: PrimitiveRestart PinnedMemory GSInstancing ClipControl DepthClamp 
31:21:391 VideoBackends/OGL/OGLConfig.cpp:744 W[Video]: Missing OGL Extensions: PrimitiveRestart PinnedMemory GSInstancing ClipControl DepthClamp 
31:29:571 VideoCommon/OpcodeDecoding.h:166 W[Video]: Warning: An error occurred.
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 400
running: ut: -u /home/phablet/dolphin-ut-user-w (for 400s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
^C
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 400
running: ut: -u /home/phablet/dolphin-ut-user-w (for 400s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 400s
32:45:740 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
32:52:307 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
32:54:890 VideoBackends/OGL/OGLConfig.cpp:744 W[Video]: Missing OGL Extensions: PinnedMemory GSInstancing ClipControl DepthClamp 
32:55:146 VideoBackends/OGL/OGLConfig.cpp:744 W[Video]: Missing OGL Extensions: PinnedMemory GSInstancing ClipControl DepthClamp 
33:18:277 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/StateSaves/GZLP01.s01.dtm does not exist
34:05:943 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/StateSaves/GZLP01.s01.dtm does not exist
34:08:731 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
35:27:259 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
36:20:340 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
37:59:083 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/StateSaves/GOWP69.s03.dtm does not exist
38:03:526 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ 

phablet@ubuntu-phablet:~/dolphin-ut-user-w$ cd /home/phablet/dolphin-ut-user-w
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ cp run-ut.log run-ut-2.log
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 100
running: ut: -u /home/phablet/dolphin-ut-user-w (for 100s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 100s
40:46:229 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
42:25:643 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ 

The only hiccup I noticed during the tests is, Wind Waker failed to load state (and it crashed for some reason), and after I made a new save state, the load state worked. If this has nothing to do with us, we can ignore that. 

## Results (2026-11-16)

All five tests passed:

- **M1 (global settings).** The pages look right, the labels are the Android app's, and switches, choice dialogs and sliders respond. GameCube Input and Wii Input open the Controllers page.
- **M2 (settings take effect).** Settings apply. Show FPS appears as soon as it is set, and Internal Resolution 2x is visibly sharper.
- **M3 (game settings).** Video Backend OpenGL for Need for Speed only works as expected. The "•" mark, press-and-hold to clear, and Clear Game Settings all behave as described.
- **M4 (CPU Boost).** The difference is "EXTREMELY clear". Need for Speed - Most Wanted runs at about 38–50 % of full speed with the boost off, and 82–100 % with it at Maximum. This confirms the 3.0 benchmark (41 % against 79 %) during real play, and supports On as the default (3.8).
- **M5 (emulated CPU clock 85 %).** Races hold a fairly stable 30–35 fps, with VPS at 50 most of the time. That matches the Android app on the same phone. It is a good per-game setting for Need for Speed. The global default stays unchanged.

### The Wind Waker crash (exit status 133)

**What happened.** Loading Wind Waker's old save state in slot 1 ended the app with SIGTRAP (signal 5, status 133). A new state saved afterwards loaded fine. The last log line before the crash was `VideoCommon/OpcodeDecoding.h:166 W[Video]: Warning: An error occurred.`

**The trigger (not the port's).** The line at `OpcodeDecoding.h:166` is `ASSERT_MSG(VIDEO, stream_size_temp < 16, ...)`. The old state's GPU command data was invalid. That state was most likely saved during a Vulkan GPU hang (the Wind Waker Vulkan issue left for stage 4), so its GPU state was already corrupt.

**The crash (the port's).** Dolphin reports an assertion through its message alert and asks "Ignore and continue?". `ASSERT_MSG` calls `Crash()` (`__builtin_trap`, so SIGTRAP) when the answer is no. DolphinQt and the Android app register a handler that asks the user. dolphin-ut registered none, so on Linux Dolphin's default handler printed the message and answered no. Every assertion therefore ended the app.

**The fix.** `main.cpp` now registers a message alert handler (`OnMsgAlert`) before anything else. It:
1. logs the message (`alert (caption): text`);
2. shows its first paragraph on screen for 8 s, in red (yellow for information);
3. answers yes, which is what choosing "Ignore" does in DolphinQt.

What follows an ignored assertion is up to Dolphin, as on desktop. With a corrupt state the picture may be wrong until another state is loaded or the game restarts, but the app no longer dies.

A real Yes/No dialog is left for the second pass. The thread that asks can be the one that draws the game picture, and the game window has no Lomiri dialog. So the question can't simply wait for a tap.

Checked on the phone. Starting a missing file (`--start /home/phablet/no-such-game.iso`) went through the handler (`alert (Warning): The specified file "…" does not exist — shown`), and the port returned to the library. The old slot 1 state was overwritten by the new save, so the Wind Waker case itself can't be repeated. Without a game running the message only reaches the log; a dialog in the library window is part of the same follow-up.

**Crash data.** `run-dolphin.sh` deletes the previous core dump and memory map when it starts, so this crash's data was gone by the next run. It didn't matter here: the log line was enough. A core dump of anonymous private memory was still 70 GB of address space (sparse, the JIT and arena reservations).

### Status

**3.6 first pass done.** Second pass: settings from the in-game menu, a Yes/No answer for Dolphin's alerts, Hotkeys, Achievements, and the remaining skipped items listed above.

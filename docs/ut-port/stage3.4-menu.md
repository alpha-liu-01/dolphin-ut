# Stage 3.4: in-game menu

Goal (`stage3-roadmap.md`): the Android app's in-game menu (`MenuFragment`), drawn into the game picture (user decision, 3.4).

Status (2026-11-15): on-screen tests done. Two fixes (closing the menu, spurious "State not found" messages) wait for a re-test (end of this document).

## What was built

The Android app's menu (`fragment_ingame_menu.xml`, `MenuFragment`, `EmulationActivity.handleMenuAction`), with its labels, as an ImGui panel along the left edge of the game picture:
- the game's title on top;
- its entries below;
- the game keeps running behind it unless paused, as in the Android app.

Dolphin's on-screen messages and statistics move right of the panel (`OSD::SetObscuredPixelsLeft`, which the Android app also sets for its menu).

| Android entry | Port | Calls |
| --- | --- | --- |
| Pause / Unpause Emulation | One entry, its label following the state; the menu stays open | `Emulator::togglePause` |
| Take Screenshot | Closes the menu | `Core::SaveScreenShot()` (`ScreenShots/`) |
| Quick Save / Quick Load | Slot 9, as the Android app | `State::Save/Load(9)` |
| Save State / Load State | A page of six slots, "Slot N" with when it was saved ("4 h ago") or "Empty" (`SaveLoadStateFragment`); the Android app's slots 0–5 | `State::Save/Load(0–5)`, `State::GetUnixTimeOfSlot` |
| Settings | **Not yet** (3.6) | |
| Overlay Controls | The 3.3 menu; its "Close" became "Back" (to the in-game menu) | |
| Refresh Wii Remotes (Wii games) | | `WiimoteReal::Refresh()` |
| Change Disc | The Android app opens a file picker; here, a list of the disc images in the game folders (no WAD/DOL/ELF) | `DVDInterface::ChangeDisc` |
| Skylanders / Infinity Base | Deferred (roadmap) | |
| Reset Emulation | | `ProcessorInterface::ResetButton_Tap()` |
| Exit Emulation | Stops and returns to the library | |

The save state entries appear only with **Core.EnableSaveStates** on. That is Dolphin's setting, off by default, and the Android app hides them the same way. Until the settings screens (3.6), it has a switch on the Developer page: "Enable Save States". Whether the port turns it on by default is a 3.8 decision for the user.

**Opening the menu.**
- A round **"Menu"** button at the top centre replaces 3.0–3.3's Exit, Save, Load and Controls buttons.
- **Back / Escape** toggles it, as the Android app's Back does; a long press still stops the game (3.2).

All actions run on the UI thread (posted from the menu, which runs on the video thread): saving a state from the video thread could wait on itself.

## Findings

### A paused game presents nothing: the menu froze

Dolphin presents a frame only when the emulated video interface sends one (`Presenter::ViSwap`). Paused, the emulated CPU is stepping and nothing reaches the screen, so a menu drawn into the picture would freeze. After "Pause Emulation" it could not even be used to unpause.

The fix uses only existing core paths, so it needs no core change:
- While the game is paused and a menu page is open, `Emulator` asks for a redraw about 30 times a second.
- Each redraw is a job queued on the paused CPU thread (`Core::RunOnCPUThread`, which runs jobs while stepping).
- That job pushes an `AsyncRequests` event; the CPU thread is the queue's producer.
- The event triggers `before_present_event`, which only the overlay listens to, and then calls `Presenter::Present()`. That re-presents the last game frame with the ImGui frame and starts the next one.
- In dual core, the GPU loop runs these events even while paused. In single core, `AsyncRequests` runs them at once on the CPU thread, which then owns the video backend.

Checked over adb with a test option, `--pause-after MS`, which pauses the game after that long: the open menu changed from "Pause Emulation" to "Unpause Emulation" after the pause, so it keeps drawing.

### Save state slots: the Android numbering

The Android app saves to slots 0–5 (shown as 1–6) and quick-saves to 9. The port does the same, so state files are interchangeable with the Android app's.
- 3.0's benchmark save (`GOWP69.s01`, saved from the old overlay button into slot 1) shows as **"Slot 2"**.
- `--state-slot 1` keeps loading that file; the option takes Dolphin's slot number.

### Test aids

- `DOLPHIN_UT_OVERLAY_MENU=game|save|load|disc|…` now opens the page when the game first draws (it used to open before boot, when no game ID was known and every slot read "Empty").
- `--pause-after MS` pauses the game after that long.

## Manual tests

Screen on, unlocked. From an interactive `adb shell`: `sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 900`. First, in the library: ⋮ → Developer → **Enable Save States** on.

**S3.4-M1. Menu.** Start Need for Speed and tap **Menu** (top centre). In landscape and upright:
- does the panel fit, with every entry reachable? Yes.
- tap outside the panel: nothing happens to the game. Does Menu or Escape (USB keyboard) close it?
The menu button is gone when the panel is toggled. There isn't a way to close it unless some action is done. 
Since there isn't a universal back button like Android, we need a separate return button on the panel itself (or do we?)

**S3.4-M2. Pause.** Pause Emulation: the game stops and the entry reads "Unpause Emulation"; the menu stays usable; Unpause resumes.

Works as expected.

**S3.4-M3. States.**
- Save State → Slot 1, drive a bit, then Load State → Slot 1: back to that point? The slot shows "Just now" or "… min. ago".
- Quick Save, then Quick Load: same.
- Load State → Slot 2 is the benchmark save from 3.0.

Works as expected, mostly. The only issue is, when there are 2 slots taken, it shows 4 state now found error messages when it saves or loads. When there are 3 slots taken, it shows 3, etc. 

**S3.4-M4. Others.**
- Take Screenshot: a message on screen, and a PNG in `/home/phablet/dolphin-ut-user-w/ScreenShots/`.
- Reset Emulation: the game restarts.
- Overlay Controls → Back returns to the menu.
- Exit Emulation: back to the library.

All work, but the screenshot is stored in a folder that is not accessible from the gallery app, but accessible from the file manager app (and it takes a password input). I know it's following the same rule as the Android build as for the save folder, but we might need to change it for ubuntu touch. Should it be done at all? If so, should it be fixed now?

**S3.4-M5 (Wii, Newer SMB).** Refresh Wii Remotes is there (it only matters for real Wii Remotes). Change Disc lists the disc images; changing to another game's disc is not useful, so only check that the list appears and Back works.

All work. 

Send the terminal output and the answers, and decide whether save states should be enabled by default (Android: off).


phablet@ubuntu-phablet:~$ sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 700
running: ut: -u /home/phablet/dolphin-ut-user-w (for 700s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 700s
24:53:269 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
26:39:314 VideoCommon/FrameDumper.cpp:210 W[Video]: FrameDump: Dolphin was not compiled with FFmpeg, using fallback option. Frames will be saved as PNG images instead.
26:57:459 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/StateSaves/GOWP69.s00.dtm does not exist
28:14:300 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/StateSaves/GOWP69.s00.dtm does not exist
29:00:533 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/StateSaves/GOWP69.s00.dtm does not exist
30:48:990 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
32:28:809 VideoCommon/FrameDumper.cpp:210 W[Video]: FrameDump: Dolphin was not compiled with FFmpeg, using fallback option. Frames will be saved as PNG images instead.
32:35:547 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/StateSaves/SMNW03.s09.dtm does not exist
32:55:443 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/StateSaves/SMNW03.s03.dtm does not exist
33:52:341 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
34:40:552 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
34:51:405 VideoCommon/FrameDumper.cpp:210 W[Video]: FrameDump: Dolphin was not compiled with FFmpeg, using fallback option. Frames will be saved as PNG images instead.
35:00:890 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
35:14:289 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
35:20:522 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
35:28:996 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
36:32:112 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
phablet@ubuntu-phablet:~$ cd /home/phablet/dolphin-ut-user-w
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ cp run-ut.log run-ut-2.log
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ cd ~
phablet@ubuntu-phablet:~$ sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 100
running: ut: -u /home/phablet/dolphin-ut-user-w (for 100s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 100s
41:00:976 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
41:33:582 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/StateSaves/GOWP69.s01.dtm does not exist
41:44:377 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/StateSaves/GOWP69.s09.dtm does not exist
41:54:987 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
42:07:898 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
phablet@ubuntu-phablet:~$ sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 40 
running: ut: -u /home/phablet/dolphin-ut-user-w (for 40s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 40s
43:05:510 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
43:22:214 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
phablet@ubuntu-phablet:~$ 


## Results of S3.4-M1…M5 (2026-11-15)

| Test | Result |
| --- | --- |
| M1 Menu fits, every entry reachable (landscape and upright) | Yes |
| M1 Closing the menu | **No way to close it** without picking an action: the Menu button is hidden while the panel is open, and Ubuntu Touch has no system Back. Fixed (below) |
| M2 Pause / Unpause | Works; the menu keeps drawing while paused |
| M3 Save / Load State, Quick Save / Load, the benchmark slot | Work, but **"State not found" messages appear**, one per empty slot (two slots used: four messages). Fixed (below) |
| M4 Screenshot, Reset, Overlay Controls → Back, Exit | Work. The screenshot lands in the Dolphin user folder, which the Gallery app does not see (below) |
| M5 Refresh Wii Remotes, Change Disc list (Newer SMB) | Work |

Log: harmless messages only (Newer SMB's cover 404; `.dtm` files not existing, as a save state has no input recording; the frame dumper saying screenshots are PNG because FFmpeg is not built in).

### Closing the menu

The Android app closes its menu with the system Back. Ubuntu Touch has none, so:
- a **"Close"** button now sits beside the game's title;
- a **tap beside the panel** (on the game picture) closes it too.

Escape still toggles it with a keyboard.

### "State not found", once per empty slot

The Save/Load page asks Dolphin when each of the six slots was saved (`State::GetUnixTimeOfSlot`). For a slot whose file does not exist, that shows "State not found" on screen. The page now checks for the file first, as Dolphin's own `State::GetInfoStringOfSlot` does. (The Android app calls the same function for every slot, so it likely shows the same messages.)

### Screenshots and the Gallery

Dolphin saves screenshots in its user folder (`<user folder>/ScreenShots/`), as the Android app does in its own. On Ubuntu Touch the Gallery shows `~/Pictures`, so they are only reachable through the File Manager.
- Worth changing: save them to **`~/Pictures/Dolphin`** (`File::SetUserPath(D_SCREENSHOTS_IDX, …)` at start).
- **Where:** 3.7 (files and storage). In the click package, the user folder moves to the app's own data folder, and writing to `~/Pictures` needs the click's `picture_files` AppArmor policy group. Both are decided there, along with the other user-visible files (save exports, the game folder).

### Re-test (stopped here)

**S3.4-M1b.** Open the menu, then close it with **Close**; open it again and close it by tapping on the game beside the panel. Both should leave the game as it was. Inside Save State / Load State, does a tap beside the panel close the menu too?

**S3.4-M3b.** With two or three slots used, open Save State and Load State: no "State not found" messages any more?

Still open from 3.4: should save states be enabled by default (`Core.EnableSaveStates`; Android: off)?

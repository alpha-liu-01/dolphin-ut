# Stage 6.5: export and user data

Goal (`stage6-roadmap.md`): backups and moving to another phone. That means the Android app's User Data actions:
- Export User Data;
- Import User Data;
- Reset Dolphin Settings.

It also means a way to get single files out of the user folder (save states, memory cards) and into it. The user folder is not visible over USB (3.7), whereas Android's is reachable through its document provider.

Status (2026-11-16): **built and checked over adb (export, import, reset, the safety checks); on-screen tests pending** (below).

## How it works

All of it is on **Config → User Data** (`UserDataPage.qml`), under the 3.7 locations. The work is done by `UserData.{h,cpp}` (`userData` in QML), with minizip-ng (already linked for the stage 4 driver packages) on a worker thread, and only while no game runs. The page says so during a game, since the in-game Settings of 6.1 can reach it.

### Export User Data

- **What goes in:** as `UserDataActivity.exportUserData`, every file under the user folder by its relative path, and empty folders as folder entries. File times are kept. Symbolic links are left out.
- **Where it goes:** Android asks with the system's save dialog. The port writes `Documents/Dolphin/Backups/dolphin-emu-<date>-<time>.zip`, which is visible over USB and in the File Manager.
  - Afterwards the result dialog offers **Send to Another App** (below), to put the zip elsewhere.
  - An export into the user folder itself is refused.
- **Before writing,** the settings are saved (`Config::Save`) as a safeguard.
- **Progress and cancel:** the progress panel shows the file being added, n of total, and Cancel. A cancelled or failed export deletes the partial zip.
- **Files that disappear** between the listing and the zipping (a cache or log being replaced) are left out with a log line, not a failure. The first test failed that way on the dev run script's maps file.

### Import User Data

- **Picking the file:** a zip from the file picker (starting in the backup folder, else Downloads).
- **The checks,** as `isDolphinUserDataBackup`:
  - it must contain `Config/Dolphin.ini`, else Android's "This file doesn't seem to contain Dolphin user data.";
  - a zip inside the user folder is refused, since importing would delete it.
- Then Android's warning: "…All existing user data will be deleted!"
- **The import:**
  - **Every entry is checked first.** One that would land outside the user folder (absolute, or through `..`) fails the import before anything is deleted. Android checks each entry while extracting, after the deletion.
  - The user folder is then emptied and the zip extracted.
  - The game list cache goes, as on Android.
- **Then the app closes, without saving:** Android ends the process (`exitProcess(0)`). The port calls `std::_Exit(0)` from the result dialog's "Close Dolphin". A normal quit would write the old settings in memory over the imported ones (`Config::Save`, `LogManager::Shutdown`).
- **Backups from Dolphin on Android** have the same layout, so they can be imported too. Paths in them (game folders) are Android's and need changing afterwards.

### Reset Dolphin Settings

- **The reset:** as `NativeLibrary.ResetDolphinSettings`, `SConfig::ResetAllSettings`. It deletes Dolphin.ini, GFX.ini, Logger.ini and the other settings files, and clears every config layer. Android's confirmation comes first.
- **The port's own defaults then come back at once** (`PortDefaults::Apply`: dual core, save states, covers), instead of at the next start.
- **What it leaves:** saves, games, controller mappings and the GPU driver files. The chosen driver is a setting, so it goes back to the system driver at the next start.

### Single files

- **Send a File to Another App:** the file picker in the user folder (StateSaves, GC, Wii, …), then `SharePage.qml`. content-hub's picker shows the apps that take any file (`ContentHandler.Destination`, `ContentType.All`), and the file is handed over as one item. The File Manager is the useful one: it saves the file where you choose (Documents, a microSD card).
  - The picker lists every destination app, Calendar and Gallery included. content-hub cannot narrow "any file" further.
  - Run without an app ID (the dev script), Dolphin lists itself (dev and click). A click's own entry should be left out by content-hub (3.7: "an app is not listed among its own peers"). Checked in M4.
  - The same page sends a fresh backup.
- **Copy a File In:** a file from anywhere (the picker starts in Downloads), then a folder of the user data (the folder picker starts in the user folder). It asks before replacing a file; a folder outside the user data is refused. This brings in a memory card (`GC/EUR/Card A`, …) or save states from another device, which Android does through its document provider.

### Test aids

`page-harness.qml`:
- `userdata-export --test-arg ZIP`;
- `userdata-check --test-arg FOLDER`;
- `userdata-import --test-arg ZIP`;
- `userdata-reset`;
- `print-setting --test-arg NAME`;
- `share --test-arg FILE`;
- `userdata`.

`run-dolphin.sh` takes another user folder with `DOLPHIN_UT_USER=…` (used here so the import could not touch the test folder).

## Checked over adb

In a scratch user folder (`/home/phablet/dolphin-ut-s65`, removed afterwards):

| Check | Result |
| --- | --- |
| Export into the user folder | refused |
| Export | marker setting (speed 1.5) set, exported: a valid zip (`testzip` clean), 56 entries, empty folders included, its Dolphin.ini with `EmulationSpeed = 1.5` |
| Copy a File In | copied into StateSaves; a second copy → "exists" (the page asks to replace); a folder outside → "Choose a folder in the user data." |
| Not a zip / a zip without `Config/Dolphin.ini` | "This file doesn't seem to contain Dolphin user data." |
| A zip with `../s65-escaped.txt` | the import failed on that entry before deleting anything; nothing written outside; the user folder intact |
| Import | after the export, the speed set to 1.0 and a file copied into StateSaves; imported: the app closed by itself (status 0); StateSaves empty again; `EmulationSpeed = 1.5` in the file and read back as 1.5 by a new start |
| Reset Dolphin Settings | speed 1.5 → 1.0 (default); the port's defaults re-applied at once (dual core, save states, covers, `DefaultsVersion = 1`) |
| Pages | User Data with Backup and Single files; Send to Another App listing File Manager, OpenStore and the others |

Not checkable over adb: handing a file to the File Manager (it needs taps in the other app). That is M4.

## Manual tests

Screen on, unlocked, from an interactive `adb shell`. **The import replaces the user folder, so these use a scratch folder,** not the test folder:

`DOLPHIN_UT_USER=/home/phablet/dolphin-ut-s65m sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 1800`

(It starts empty: the library shows the games folder's games; settings are the defaults.)

**S6.5-M1. Export.** Settings → Config → User Data → **Export User Data**.
- Does the progress panel show the files, then "Your user data has been exported." with the zip's path?
- Is the zip in the File Manager under Documents/Dolphin/Backups?
Yes. 

**S6.5-M2. Import.** Change a setting you can see (for example Settings → Graphics Settings → Statistics → Show FPS on). Then User Data → **Import User Data** → the zip from M1.
- Android's warning? Yes. 
- "The user data has been imported. Dolphin closes now…" → **Close Dolphin**: does the app close? Yes. It closed itself.
- Start it again with the same command: is Show FPS back off (as at the export)? Yes. (I toggled enable cheats instead.)

**S6.5-M3. Reset.** Turn Show FPS on again, then **Reset Dolphin Settings** → Yes: is it off again, and is Config → General → Dual Core still on (the port's default)? Yes. 

**S6.5-M4. Send a file.** **Send a File to Another App** → open StateSaves or Config, tap a file (Config/Dolphin.ini will do) → pick **File Manager**.
- Does the File Manager open and let you save the file (in Documents, for example)? Is the file there afterwards? Yes. 
- Is Dolphin itself in the app list? (It is when run from the terminal; note whether it is.) Yes. 
- Also after M1: the result dialog's **Send to Another App** with the backup zip. This also works. 

**S6.5-M5. Copy a file in.** **Copy a File In** → the file you saved in M4 → open StateSaves → ✓. "Copied to …"? Again: does it ask to replace? Yes. 

Afterwards the scratch folder can go: `rm -r /home/phablet/dolphin-ut-s65m` (and the backup zips in Documents/Dolphin/Backups if you don't want them). Deleted. 

## Results

(to be filled in)

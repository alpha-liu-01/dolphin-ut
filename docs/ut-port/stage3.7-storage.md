# Stage 3.7: files and storage

Goal (from `stage3-roadmap.md`): games get onto the phone and saves stay put, within click confinement.

## What the phone allows

Checked on the phone (Ubuntu Touch 24.04-2.x, AppArmor policy version 2404.2):

- **MTP** (`mtp-server`) shows a computer these folders:
  - ~/Documents, Downloads, Music, Pictures and Videos;
  - /media (memory cards).
- **What a click may touch** (the `default` template plus policy groups in `/usr/share/apparmor/easyprof/`):
  - its own folders, always: `~/.local/share/<package>/`, `~/.config/<package>/` and `~/.cache/<package>/`;
  - `document_files`: ~/Documents and `/media/*/*/Documents`, read and write (`document_files_read`: read only);
  - `picture_files`: ~/Pictures and `/media/*/*/Pictures`;
  - nothing gives access to ~/Downloads.

  So **~/Documents is the one folder that both a computer (over MTP) and a confined app can reach**.
- **microSD:** the phone had no card in it while this was built (the `mmc1` slot was empty). The user tested a card afterwards (S3.7-M7, passed).
- **content-hub** (`lomiri-content-hub` 2.2.3, the QML module `Lomiri.Content` for Qt 6):
  - The File Manager is a source and a destination for "all" types.
  - An app identifies itself with `APP_ID`. For an unconfined process, content-hub trusts that variable (`service.cpp`, `CreateImportFromPeer`).
  - A file comes into the receiving app's `~/.cache/<package>/HubIncoming/<n>/` as a **hard link** when it is on the same file system, so even a 4 GB disc arrives at once and takes no extra space. From another file system, content-hub copies it (`copy_to_store`).
  - A confined app may not write in HubIncoming (`deny ... w` in the `content_exchange` group).
  - Peers are registered from files in `~/.local/share/lomiri-content-hub/` (a click's `content-hub` hook makes those), which content-hub watches.
- **The app's lifecycle:** Lomiri stops an app that is not in the foreground (state `T`; also when the screen locks). A signal to it waits until it is in the foreground again.

## Decisions

| What | Where | Why |
| --- | --- | --- |
| User folder (settings, saves, memory cards, states, NAND, caches, logs) | `~/.local/share/<package>/` when Lomiri started the app (`APP_ID` set), one folder as on Android. `-u` still wins; without either, Dolphin's default | The only place a click may write its data. Dolphin's own Linux layout splits data, config and cache over `~/.local/share/dolphin-emu`, `~/.config/dolphin-emu` and `~/.cache/dolphin-emu`, which a click may not write |
| Qt's settings (library tab and others) and caches | `~/.config/<package>/<package>.conf`, `~/.cache/<package>/` (organization and application name = the package, as Lomiri apps do) | Were in `~/.config/Dolphin Emulator/`, outside confinement |
| Games | **~/Documents/Dolphin/Games**, created at start and added to the library once (`[UbuntuTouch] GamesFolderAdded`, so removing it sticks) | Reachable over MTP and with `document_files`. Other folders can still be added |
| Screenshots | **~/Pictures/Dolphin/<game ID>/** (`D_SCREENSHOTS_IDX`, found in 3.4) | The Gallery shows ~/Pictures; `picture_files` |
| Import from other apps | content-hub, both ways: **Import Games** in the library (Dolphin asks the File Manager or another app), and **Share** in the File Manager (Dolphin is a destination for "all") | Ubuntu Touch's way; works in confinement |

## What was built

### Storage (`Storage.{h,cpp}`, context property `storage`)

- `Storage::ConfigureApplication()` (before `QGuiApplication`) and `Storage::UserFolder()` (for `UICommon::SetUserDirectory`). The package is `APP_ID` up to the first `_`: `name_app_version` for a click, or the desktop file name for a non-click app.
- `Storage::SetUp()`:
  - creates ~/Documents/Dolphin/Games and ~/Pictures/Dolphin;
  - points screenshots there;
  - adds the games folder to the library once;
  - logs the folders (`storage: user folder …`).
- `places()`: the picker's shortcuts (Dolphin games, Documents, Downloads, Home, and each volume mounted under /media/).
- `importFiles(urls)`:
  - Runs on a worker thread and moves game files (Dolphin's list of game extensions) into the games folder. It tries a rename first, then a hard link, then a copy: confinement may forbid the first two (see below), and a copy of a disc takes a while.
  - Names never collide ("name (2).iso").
  - Other files are skipped and reported.
  - `importFinished` sends the result; `Main.qml` shows it and refreshes the library.

### Pickers and paths

- `FolderPickerPage.qml` is now a general file and folder browser (`pickFiles`, `extensions`, `startFolder`, signal `picked(path)`), with the places at the top. Game Folders → Add Folder and the new items below use it.
- **Settings, the items skipped in 3.6** (`ut/tools/gen-settings.py` now generates them):
  - **paths**:
    - Default ISO;
    - Wii NAND Root;
    - Dump, Load, Resource Pack and WFS paths;
    - GBA BIOS and saves, and the Game Boy Player ROM;
    - SD Card image and SD Sync Folder.

    Each one is a `file` or `folder` item with the Android app's file filters, shown as the path in use. Tap to choose; press and hold to go back to the default.
  - `SettingsModel` applies a path at once, as DolphinQt's `ConfigUserPath` does. It rebuilds Dolphin's user paths, re-applies the configured ones and the screenshot folder, and creates the folders Dolphin expects. These items are global only.
  - **Pack SD Card Now / Unpack SD Card Now** (`action` items, with the Android confirmations): `Common::SyncSDFolderToSDImage` / `SyncSDImageToSDFolder` on a worker thread, then Done / Failed.
  - **Config → User Data** (`UserDataPage.qml`): where the games, screenshots and user data are, and the app ID.
- Library actions:
  - **Import Games** (`ImportPage.qml`): the content-hub app picker, then a progress page;
  - **Open File** (as on Android): the picker filtered to game files, then start.

  The empty library also offers Import Games and mentions Documents/Dolphin/Games.

### Development install (`ut/phone-debug/dev-install.sh`, `launch-dolphin.sh`)

So the app can be started like an installed one before stage 5's click:
- `~/.local/share/applications/dolphin-ut.desktop` puts **"Dolphin (dev)"** in the app drawer. Lomiri gives it `APP_ID=dolphin-ut`, so its user folder is `~/.local/share/dolphin-ut` (a new, empty one).
- `~/.local/share/lomiri-content-hub/dolphin-ut` registers it as a content-hub destination for "all". content-hub's registry shows it next to the File Manager (`gsettings list-recursively com.lomiri.content.hub.destination`).
- `launch-dolphin.sh` sets what `run-dolphin.sh` sets for Qt 6 and audio, and logs to `/home/phablet/dolphin-ut-launch.log` (the previous run's log: `.1`).
- `X-Lomiri-Single-Instance=true`: without it, Lomiri treats a non-click app as multi-instance, and each start from the drawer or Share from another app starts another copy (S3.7 results). A click is single-instance already.
- `sh dev-install.sh remove` undoes it.

## Checked over adb

- Started through Lomiri (`lomiri-app-launch dolphin-ut`):
  - `APP_ID=dolphin-ut`;
  - user folder `/home/phablet/.local/share/dolphin-ut/` with Dolphin's folders in it;
  - ~/Documents/Dolphin/Games created and in the library;
  - ~/Pictures/Dolphin created.
- With `APP_ID` set, Qt's settings go to `~/.config/dolphin-ut/dolphin-ut.conf`.
- content-hub lists the File Manager, Morph Browser, Gallery and others as sources. Dolphin is registered as a destination (an app is not listed among its own peers).
- **Path round trip** (`page-harness.qml`, `paths-roundtrip`):
  - Dump Path set to a test folder: the value reads back, and Dolphin's subfolders (Frames, Audio, …) appear there at once;
  - reset: back to `<user>/Dump`, and the key is gone from Dolphin.ini.
- **SD card round trip** (`sd-roundtrip`): Pack made a 512 MB `WiiSD.raw` from the sync folder, and Unpack restored its test file.
- Screenshots of Config → Paths, Open File, Import Games and User Data look right. The import page lists every app that can be a source, Camera and Contacts included; that is how Ubuntu Touch's picker works for the type "all".
- Pressing ✓ in a picker, tapping through the File Manager, and sharing from it need the screen (manual tests).

## Not in 3.7

- **Confinement itself.** The app runs unconfined, as a non-click app. The paths and policy groups above are what a click needs: `document_files`, `picture_files`, `content_exchange`, `content_exchange_source`. Checking them under the real AppArmor profile is stage 5. Gamepads (`/dev/input`) may require the `unconfined` template anyway (roadmap risk 7), which would make most limits here moot. The layout is right either way.
  - Under confinement, the rename out of HubIncoming may be refused. The hard link may be too: AppArmor link rules compare the permissions of both paths. The import then copies, on its worker thread.
- **Export** (save states, memory cards, screenshots back through content-hub) and the Android app's zip **import/export of the whole user data**. These were optional in the roadmap and stay deferred: screenshots already sit in ~/Pictures, and the rest can wait for a user who needs it.
- **Install WAD, Import Wii Save, Import BootMii NAND Backup, Load Wii System Menu, Online System Update** (library menu on Android). They stay deferred as in the roadmap. They are small now that a file picker exists.
- microSD under confinement: the picker lists cards mounted under /media/, and `document_files` allows `<card>/Documents/`. Unconfined, a game folder on a card works (S3.7-M7); confined, stage 5.

## Manual tests

Screen on, unlocked. Install the dev entry once: `sh /home/phablet/dolphin-ut-probe/dev-install.sh`. The app drawer then has **Dolphin (dev)**; if it does not show, search the drawer for it. Started from there, it uses a **new user folder** (`~/.local/share/dolphin-ut`): default settings, no game folders except Documents/Dolphin/Games, default controller setup. The old test folder (`dolphin-ut-user-w`) stays for the `run-dolphin.sh` runs. The log is `/home/phablet/dolphin-ut-launch.log`.

**S3.7-M1. Start from the app drawer.** Open Dolphin (dev). Does the library come up, empty, with the Import Games button and the Documents/Dolphin/Games hint? Settings → Config → User Data: are the three folders shown, with "App ID: dolphin-ut"?

Yes. 

**S3.7-M2. Games over USB (MTP).**
- Connect the phone to a computer, open Documents → Dolphin → Games, and copy a game into it (for example Newer SMB's .wbfs).
- Switch back to Dolphin (dev): the library refreshes when the app comes back. Does the game appear, and does it run?

Yes for all questions. 

**S3.7-M3. Import Games.**
- Library ⋮ → Import Games → File Manager. Go to `/home/phablet/dolphin-ut-games` (or Downloads) and pick a game.
- Is it back in Dolphin at once, with "1 game imported", and in the library?
- The original file stays where it was. On the same storage, the copy in Documents/Dolphin/Games is a second name for the same data (a hard link), so no space is used twice.

Yes. 

**S3.7-M4. Share from the File Manager.** In the File Manager, long press a game file, Share (or Open with), and choose Dolphin (dev). Is Dolphin offered? Does the game get imported, with the same result dialog?

Yes. 

**S3.7-M5. Screenshots.** In a game, open the menu → Take Screenshot. Is it in the Gallery (Pictures/Dolphin/<game ID>/)?

Yes. 

**S3.7-M6. Open File and paths.**
- Library ⋮ → Open File → Home → dolphin-ut-games → tap a game: does it start?
- Settings → Config → Paths → Dump Path: choose a folder with ✓, check the path shown, then press and hold → Reset.

Everything works as expected. 

**S3.7-M7 (if you have a microSD card).** Insert it. Does the picker list it under the places? Can a game in `<card>/Documents/Dolphin/Games` be added as a game folder and started?

Yes. 

Send the answers, and the log if something fails (`/home/phablet/dolphin-ut-launch.log`).

But I still noticed 2 issues. 

1. All quick save/load/save state/load state options are GONE from the in-game menu. Is it supposed to happen at this stage, or is it a bug that needs to be fixed right away? 

2. By the end of my test, I saw **3** instances of Dolphin in the recent app view. This is not supposed to happen. 

## Results (2026-11-16)

**All seven tests passed** (M1–M7): the start from the app drawer, games over MTP, Import Games, Share from the File Manager, screenshots in the Gallery, Open File and a path setting with reset, and a game folder on a microSD card.

The user reported two issues.

**1. The save state items were gone from the in-game menu.** This is expected, not a regression:
- The menu shows Quick Save, Quick Load, Save State and Load State only with Dolphin's **Enable Savestates** on (`Core.EnableSaveStates`, off by default), as the Android app does (3.4).
- The run-dolphin.sh test folder (`dolphin-ut-user-w`) had it on (`EnableSaveStates = True` in its Dolphin.ini). Dolphin (dev) starts with a new user folder, so it was off.
- To get them back: Settings → Config → General → Enable Savestates.
- Whether the port turns it on by default is still the open 3.8 decision.

**2. Three copies of Dolphin in the app switcher.** This was a bug in the development entry:
- The logs show copies running side by side. One started at 09:36:26 and was still writing at 09:47; another started at 09:38:06, then imported a game shared from the File Manager (its game list went from 2 to 3 files four seconds later). A third start's log was lost to the rotation.
- Cause: lomiri-app-launch treats a non-click (legacy) desktop entry as **multi-instance** unless the desktop file has `X-Lomiri-Single-Instance=true` (one of the keys `liblomiri-app-launch` reads). Each start from the drawer, and each Share from content-hub (which launches its destination), started a new copy. Two copies on one user folder can overwrite each other's settings and saves.
- Fix: `dev-install.sh` writes the key (re-run on the phone). Checked over adb: `lomiri-app-launch dolphin-ut` twice leaves one `dolphin-ut` process, and the second call focuses it. The click in stage 5 is single-instance by default, so this was specific to the development entry.

Re-test (with the new entry; close every Dolphin copy in the app switcher first):

**S3.7-M8. One copy only.**
- Open Dolphin (dev) from the drawer, go back to the home screen, and open it from the drawer again.
- Then in the File Manager, Share a game to Dolphin (dev).
- Is there one Dolphin in the app switcher at the end, and did the share import into it (result dialog, game in the library)?


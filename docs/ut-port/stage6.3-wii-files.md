# Stage 6.3: Wii file actions

Goal (`stage6-roadmap.md`): the Android app's Wii items from its main menu:
- Install WAD;
- Import Wii Save;
- Import BootMii NAND Backup;
- Load Wii System Menu;
- Perform Online System Update.

Also its game properties' "Perform System Update" for Wii discs.

Status (2026-11-16): **done.** Checked over adb (all failure paths) and on screen (S6.3-M1–M4; M5 skipped for want of files). The Wii Menu installs through the online update and runs. Starting a GameCube disc from the Wii Menu crashes in Dolphin's core (below, "The crash"): the user's decision.

## How it works

Everything runs Dolphin's own code, as the Android app's JNI (`Source/Android/jni/WiiUtils.cpp`) and DolphinQt call it:
- `WiiUtils::InstallWAD`;
- `WiiSave::Import`;
- `DiscIO::NANDImporter::ImportNANDBin`;
- `WiiUtils::DoOnlineUpdate` and `DoDiscUpdate`;
- the Wii Menu boots as `BootParameters::NANDTitle{Titles::SYSTEM_MENU}`.

Nothing changes how the emulated Wii behaves.

### Where it is

| Android | The port |
| --- | --- |
| Main menu (overflow): Load Wii System Menu (with its version), Perform Online System Update, Install WAD, Import Wii Save, Import BootMii NAND Backup | Library ⋮ → **Wii System** (`WiiSystemPage.qml`): the same five, on one page. The library's menu already had nine items. The header shows "Wii Menu 4.3E" (or "vWii Menu …", or "not installed"), and the first item is "Load Wii System Menu (4.3E)", as Android's menu title |
| Game properties: "Perform System Update" (Wii discs) | The library's long press: **Perform System Update**, shown for Wii discs only (`GameList::details` gains `wiiDisc`). It opens the Wii System page, which runs the update |

### `WiiSystem` (`WiiSystem.{h,cpp}`, `wiiSystem` in QML)

- **The Wii Menu's state:** `refresh()` reads the installed Wii Menu's TMD, as `WiiUtils.isSystemMenuInstalled`, `isSystemMenuvWii` and `getSystemMenuVersion` do. It gives installed, vWii, and the version (`DiscIO::GetSysMenuVersionString`).
- **Running the actions:**
  - Each action runs on a worker thread, one at a time, and only while no game runs. A second IOS beside the game's would share its NAND files; the library is hidden during a game anyway.
  - `finished(task, ok, title, message)` reports the result with the Android app's texts.
  - Afterwards the Wii Menu's state is read again.
- **Install WAD:** `WiiUtils::InstallWAD`. "Successfully installed this title to the NAND." or "Failed to install …".
- **Import Wii Save:** `WiiSave::Import`. If the save already exists, the worker asks the page (`overwriteAsked` → `answerOverwrite`), as `MainPresenter.importWiiSave` does with its latch. The results are Android's four texts; a "No" is silent.
- **Import BootMii NAND Backup:**
  - The warning comes first, then the file, as DolphinQt; Android picks first.
  - Progress ("Loading NAND…", "Extracting NAND…") and Cancel come from DolphinQt; Android has neither.
  - **Keys file:** a backup without keys appended asks for the keys file (`keysFileAsked` → the file picker → `provideKeysFile`), as DolphinQt. Android only reports that the keys are missing. The picker gained a `cancelled` signal, so leaving it gives up cleanly.
  - **The result:** `ImportNANDBin` returns nothing. It shows some failures as Dolphin's alerts (which reach the 6.1 dialog) and only logs others. On success it reports the extraction complete once more at the very end. The port uses that to say "imported" or "could not be imported".
- **Online System Update:** `WiiUtils::DoOnlineUpdate` with progress ("Updating title 0000000100000002…", n of total) and Cancel, as `SystemUpdateProgressBarDialogFragment`.
  - The region is the installed Wii Menu's.
  - Without a Wii Menu (also for "Load Wii System Menu"), the page asks Android's "Not installed … Roughly 110 MiB …", then for a region: Europe, Japan, Korea or USA (`OnlineUpdateRegionSelectDialogFragment`).
  - After a successful update, the certificates are extracted from the NAND (`NANDImporter::ExtractCertificates`), as DolphinQt does; Android does not.
- **Disc update:** `WiiUtils::DoDiscUpdate`, the same progress, results and certificates.
- **Result texts:** the update's titles and messages are Android's (`SystemUpdateResultFragment`). "Disc read failed" has the "no usable update information" text, as on Android and in DolphinQt.

### The page

- **Progress:** a panel on the page itself rather than a popup, so a file picker pushed for the keys question covers it. It has a progress bar (indeterminate when the total is unknown) and Cancel for the updates and the NAND import.
- **Leaving:** Back is disabled while an action runs, so its result is shown.
- **Files** are picked with the port's picker, starting in Downloads. The click runs with the `unconfined` AppArmor template (stage 5), so Downloads and microSD cards are readable.

### Load Wii System Menu

`Emulator::startSystemMenu()` calls `start()` with a marker path (`SYSTEM_MENU_PATH`). The boot is then `BootParameters::NANDTitle{Titles::SYSTEM_MENU}`, as `NativeLibrary.RunSystemMenu`. With the marker:
- the restart on OpenGL ES after a Vulkan failure (stage 4) works for it unchanged;
- the in-game menu's title is "Wii System Menu";
- the overlay is the Wii Remote's, with the pointer: a finger on empty screen points, and a double tap presses A (stage 3.3).

### Test aids

`page-harness.qml`:
- `wii`: the page;
- `wii-roundtrip --test-arg FOLDER --test-arg2 DISC`: the Wii Menu's state, then each action's failure path with dummy files, then a disc update.

## Checked over adb

Test build, test user folder:

| Check | Result |
| --- | --- |
| Wii Menu state | not installed (the test NAND has no Wii Menu) |
| Install WAD, a text file | "Failed to install this title to the NAND." |
| Import Wii Save, a text file | "Failed to import save file. The given file appears to be corrupted or is not a valid Wii save." |
| Import NAND backup, a text file | Dolphin's alert "This file does not look like a BootMii NAND backup." in the 6.1 dialog (answered by `--answer-after`), then "The NAND backup could not be imported. …" |
| Disc update from Newer Super Mario Bros. (.wbfs) | "Update failed: The game disc does not contain any usable update information." (WBFS drops the update partition) |
| The page | as intended (screenshot): header "Wii Menu not installed", the five items with explanations |

The test NAND was not changed (every action failed before writing), and the dummy files were removed.

## Manual tests

Screen on, unlocked, **on Wi-Fi**, from an interactive `adb shell`. The test build is deployed; the installed click does not have 6.1–6.3.

`sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 1800`

**S6.3-M1. Install the Wii Menu.** Library ⋮ → **Wii System**. The header says "Wii Menu not installed".
- Tap **Load Wii System Menu**: does "Not installed … Roughly 110 MiB …" come up? Yes, then **Europe** (your games are PAL).
- Does the progress panel show "Updating title …" with a growing bar? Yes. 
- Does it end with "Update completed: The emulated Wii console has been updated."? Yes. 
- Does the header then show the version (for example "Wii Menu 4.3E")? Yes. 

**S6.3-M2. Load the Wii Menu.** Tap **Load Wii System Menu (4.3E)**.
- Does the Wii Menu start, after the health and safety screen? Point with a finger on empty screen, and double-tap or press A to go on. Yes. 
- Does the channel grid show? Can you open a channel, for example Settings (the Wii button)? Yes. 
- Menu → Exit: back to the library? Not done yet. 


```
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 1800
running: ut: -u /home/phablet/dolphin-ut-user-w (for 1800s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
exited by itself after 437s (status 139)
crashed (signal 11); crash data:
PC              0x5581f09ef8  /home/phablet/dolphin-ut-arm64/dolphin-ut+0x304ef8 (r-xp)
LR (R30)        0x5581eb5864  /home/phablet/dolphin-ut-arm64/dolphin-ut+0x2b0864 (r-xp)
Access address  0x5c0  
  core dump: -rw------- 1 phablet phablet 1331388416 Nov 16 20:55 core
  crash data kept in /home/phablet/dolphin-ut-user-w/crash-1116-205545-ut
48:28:605 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
48:29:808 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
49:05:507 Core/WiiUtils.cpp:566 W[CORE]: Importing required system title 0000000100000050 first
50:46:725 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
50:47:739 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
51:21:517 VideoBackends/Vulkan/VulkanLoader.cpp:84 W[Host GPU]: Loading system driver
51:21:531 VideoBackends/Vulkan/VulkanContext.cpp:216 W[Host GPU]: Using Vulkan 1.2, supported: 1.3
51:21:777 VideoBackends/Vulkan/VulkanLoader.cpp:84 W[Host GPU]: Loading system driver
51:21:781 VideoBackends/Vulkan/VulkanContext.cpp:400 W[Video]: Vulkan: Debug utils requested, but extension is not available.
51:21:781 VideoBackends/Vulkan/VulkanContext.cpp:216 W[Host GPU]: Using Vulkan 1.2, supported: 1.3
51:22:904 Core/HW/MMIO.cpp:177 E[MI]: Trying to read 32 bits from an invalid MMIO (addr=0d800024)
51:26:528 Core/HW/MMIO.cpp:177 E[MI]: Trying to read 32 bits from an invalid MMIO (addr=0d800024)
51:26:538 Core/HW/MMIO.cpp:177 E[MI]: Trying to read 32 bits from an invalid MMIO (addr=0d800018)
51:26:557 Core/IOS/IOS.cpp:762 W[IOS]: Previous request to device /dev/fs blocked emulation for 4569 microseconds.
51:26:895 Core/IOS/IOS.cpp:762 W[IOS]: Previous request to device /dev/fs blocked emulation for 5663 microseconds.
51:26:900 Core/IOS/IOS.cpp:762 W[IOS]: Previous request to device /dev/fs blocked emulation for 4211 microseconds.
51:27:001 Core/IOS/IOS.cpp:762 W[IOS]: Previous request to device /dev/es blocked emulation for 5379 microseconds.
51:27:056 Core/IOS/USB/Bluetooth/BTEmu.cpp:1568 E[IOS_WIIMOTE]: HCI: CommandDeleteStoredLinkKey... Probably the security for linking has failed. Could be a problem with loading the SCONF
51:28:445 Core/IOS/IOS.cpp:762 W[IOS]: Previous request to device /dev/es blocked emulation for 2824 microseconds.
51:30:085 Core/IOS/IOS.cpp:762 W[IOS]: Previous request to device /dev/fs blocked emulation for 3028 microseconds.
```

I was doing something out of scope here. I went into the disk channel of the wii menu, hit change disk and loaded the NFS MW ISO, and hit start. I'm not sure if this is supposed to work at all, but this caused a crash. Before the crash log is gone, please check it out and see if it's an issue at all. (This sounds like a stupid thing to do... )

**S6.3-M3. Update again.** Wii System → **Perform Online System Update**: "The emulated Wii console is already up-to-date."?
Yes. 

**S6.3-M4. Disc update.** In the library, long press **Newer Super Mario Bros.**. Is there **Perform System Update**? Tap it: "Update failed: The game disc does not contain any usable update information." (a WBFS file has no update partition). Is there no such button on Need for Speed (GameCube)?
Yes. Update failed as expected. No such button on GameCube games. 

**S6.3-M5 (only if you have the files).** Install WAD (your own .wad), Import Wii Save (a data.bin from a Wii's Data Management), Import BootMii NAND Backup (your own nand.bin, with keys.bin if it asks): the result messages?

I have to skip this because I don't have the files. 

## Results

2026-11-16: M1–M4 passed; M5 skipped (no WAD, save or NAND backup files at hand).

| Test | Result |
| --- | --- |
| M1 Install the Wii Menu | Pass. "Not installed" came up → Europe → "Updating title …" with a growing bar → "Update completed". The header then showed the version. Dolphin also logged "Importing required system title 0000000100000050 first" (IOS80, which the Wii Menu needs) |
| M2 Load the Wii Menu | Pass. The Wii Menu started (health screen passed with the pointer and A); the channel grid showed, and a channel opened. The exit was not written down, but the log of the later session shows it: Wii Menu started 21:30:37, "stop requested" 21:33:55, idle 0.16 s later, the library back, no crash |
| M3 Update again | Pass: "The emulated Wii console is already up-to-date." (1 s: only the update list was fetched) |
| M4 Disc update | Pass: "Perform System Update" on Newer Super Mario Bros., not on Need for Speed (GameCube). It failed as it should, though with a different reason than the test expected: "The game's region does not match your console's …". With the European Wii Menu now installed, `DoDiscUpdate` checks the region before the update partition, and Newer's ID SMNW03 has region letter W, not European. The test was written before a Wii Menu was installed |
| M5 WAD, save, NAND backup | Skipped (no files). Only the failure paths are tested (over adb, above); the success paths run Dolphin's own code unchanged |

### The crash: a GameCube disc started from the Wii Menu (outside the tests)

**What happened:**
- In the Wii Menu: the in-game menu's Change Disc → Need for Speed (a GameCube disc) → the Disc Channel → Start.
- The app crashed with SIGSEGV 0.2 s after Dolphin logged "BC: Launching MIOS…", "Reinitialised hardware" and "Loaded MIOS and bootstrapped PPC".
- Crash data: `dolphin-ut-user-w/crash-1116-205545-ut`.

**Where:**
- PC in `IOS::HLE::EmulationKernel::UpdateIPC()`, a read at address 0x5c0: a member read through a null kernel.
- The return addresses on the stack (`core-stack.py`, new in `ut/phone-debug`, then `addr2line` on the same build):

```
WiiIPC::RegisterMMIO … lambda (a Wii IPC register write)   ← system.GetIOS()->UpdateIPC()
MMIO::WriteHandler<u32>::Write
Interpreter::SingleStepInner / SingleStep
IOS::HLE::MIOS::Load
IOS::HLE::EmulationKernel::EmulationKernel        (title 1-101, MIOS)
CoreTiming … Advance                              (the IOS boot event: FinishIOSBoot)
JitArm64::Run / PowerPCManager::RunLoop / CPUManager::Run / Core::CpuThread
```

**Why (Dolphin's core, not the port):**
- A GameCube disc in the Wii Menu launches BC, which Dolphin replaces with MIOS (`ESDevice::LaunchIOS`).
- `FinishIOSBoot` (`Core/IOS/IOS.cpp`) first drops the running IOS (`system.SetIOS(nullptr)`), then constructs the MIOS kernel.
- For MIOS, that constructor calls `MIOS::Load`, which runs the GameCube IPL in the interpreter until the IPL signals it has started. So the IPL runs while `system.GetIOS()` is null.
- The IPL wrote a Wii IPC register. The write handlers in `Core/HW/WII_IPC.cpp` call `system.GetIOS()->UpdateIPC()` without a check, so it crashed.
- Nothing from the port is on the stack. Change Disc only inserted the disc, 27 s before, and the Wii Menu read it normally.
- The same code runs on every platform. Both pieces date from the 2024 change that moved the IOS instance into `Core::System` (e6ee217a7c).
- A web search found no report of this exact crash. The closest: [issue 11952](https://bugs.dolphin-emu.org/issues/11952?tab=notes) (an older failure on the same GameCube-from-Wii-Menu path) and [issue 13963](https://bugs.dolphin-emu.org/issues/13963) (a recent Wii Menu crash).

**What to do:**
- It changes how the emulated Wii behaves (IOS/MIOS), so by the repository's rules it is not the port's to change. It is the user's decision: fix it, report it upstream, or leave it.
- Possible fixes:
  - in `FinishIOSBoot`, keep the old kernel until the MIOS kernel exists;
  - or let the IPC write handlers skip `UpdateIPC()` while no IOS is set (MIOS has no IPC anyway).
- Starting GameCube games from the library is the supported way and is unaffected.

The crash folder holds a 1.3 GB core (sparse); it can go once nobody needs it.

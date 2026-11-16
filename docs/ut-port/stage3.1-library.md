# Stage 3.1: game library

Goal (`stage3-roadmap.md`): the Android app's main screen. Open the app, see the games, start one. The look is native Ubuntu Touch (Lomiri Components), as the user decided in 3.0.

Status (2026-11-15): **done.** The on-screen tests passed except the remembered tab, which is fixed and checked over adb (results at the end).

## What was built

| Part | Where | Notes |
| --- | --- | --- |
| Game list model | `Source/Core/DolphinUT/GameList.{h,cpp}`, `GameListModel` | Over `UICommon::GameFileCache`, as DolphinQt's `GameTracker` and the Android `GameFileCacheManager` do. The cache, the folder scan, reading new files and downloading covers all run on a worker thread ("Game list"). The cache is loaded first, so known games show at once; then the folders are scanned. Roles: path, title (with Dolphin's title database), company, game ID, platform tab, cover |
| Tabs and search | `GameListFilter` (`DolphinUT.Native`) | The Android app's three tabs: **GameCube** (with Triforce), **Wii**, **WiiWare** (with ELF/DOL). Android has no "All" tab, and neither does the port. Sorted by title. While searching, all platforms; matches title or game ID |
| Covers and banners | `GameImageProvider` (`image://dolphin/cover/…`, `…/banner/…`) | From Dolphin's own GameTDB download (`GameFile::DownloadDefaultCover`, cached in `Cache/GameCovers`) and the game's banner |
| Library page | `qml/LibraryPage.qml`, `qml/GameCard.qml` | Header: title, Refresh, Search, and an overflow menu (Grid Options, Game Folders, Developer). Platform tabs as Lomiri `Sections`. A grid of cover cards (about 16 grid units: 3 columns upright, 7 in landscape), the title on a plain card when there is no cover, title and company below (`card_game.xml`). Pull to refresh; a progress line while scanning; an empty state that leads to Game Folders. The tab is remembered |
| Tap / long press | | Tap starts the game with the configured backend. Long press opens the game properties dialog. It has **Details**, and for now **Start with Vulkan** and **Start with OpenGL ES** (for this run only), until per-game settings (3.6) exist. The other Android entries come with their phases |
| Game details | `qml/GameDetailsPage.qml` | The Android dialog's content as a page: banner, title, description, country, company, game ID, revision, file size or format, compression, block size, plus the file path |
| Grid options | dialog in `LibraryPage.qml` | As Android: "Download Game Covers from GameTDB" (`General.UseGameCovers`) and "Show Titles" (`UbuntuTouch.ShowGameTitles`, the port's own section; Android keeps it in `Android`) |
| Game folders | `qml/GameFoldersPage.qml`, `qml/FolderPickerPage.qml` | Dolphin's ISO paths: list, swipe to remove, "Search Subfolders" (`General.RecursiveISOPaths`). A folder browser to add one (Ubuntu Touch has no system folder picker; 3.7 handles confinement and content-hub) |
| Rescan on return | `qml/Main.qml` | Like the Android app, the library rescans when the user comes back to the app (games copied over USB show up), unless a game is running |
| Developer page | `qml/DeveloperPage.qml` | The 3.0 test shell's tools: log, the two spike switches, Vibrate, keyboard test. Not an Android feature |

`Emulator`'s old file list is gone. `start(path)` without a backend now uses the configured one.

## Findings

### Lomiri Components on Qt 6 scale by themselves: no `QT_SCALE_FACTOR`

`qml6-module-lomiri-components` (1.3.5906, the files under `2.0/`) is on the phone. The installed apps that use Lomiri Components are all Qt 5. In 3.0, `dolphin-ut` set `QT_SCALE_FACTOR = GRID_UNIT_PX / 8` (2.625) for Qt Quick Controls. With Lomiri Components that **scales everything twice**:
- `units.gu(1)` is 21 logical pixels whatever Qt's scale is;
- at a scale of 2.625 that makes 55 physical pixels.

The fix is the same as Qt 5 Lomiri apps: no Qt scale factor (device pixel ratio 1), with Lomiri's grid units doing the scaling. Checked from screenshots:
- double size with the factor;
- the native size without it.

`main.cpp` no longer sets it. The game window and overlay already work in physical pixels.

There is **no Qt 6 Suru style for Qt Quick Controls** on the phone (only Basic, Fusion, Imagine, Material, Universal), so the app uses Lomiri Components throughout and no longer forces Material.

### No Qt 6 `qml` runtime: two developer options

`/usr/bin/qml` is qtchooser's (Qt 5). To check pages without the user, `dolphin-ut` gained two options:
- `--qml FILE` loads another QML file with the same context objects;
- `--grab PNG [--grab-after MS]` saves a screenshot of the app window.

`ut/phone-debug/page-harness.qml` opens a given page over the library. Run it through `run-dolphin.sh ut` (see its header).

### HTTPS from the bundled curl had no CA store

The first cover downloads failed with "mbedTLS: The certificate is not correctly signed by the trusted CA". The cause: curl's CMake only looks for a CA bundle when not cross-compiling. With `USE_SYSTEM_LIBS=OFF` in the container, curl was built with no CA bundle or path at all.

`ut/build.sh` now passes:
- `-DCURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt`;
- `-DCURL_CA_PATH=/etc/ssl/certs`.

These are Ubuntu Touch's usual Debian locations. After that, the Need for Speed cover downloaded (`GOWP69.png`). Newer SMB has none on GameTDB (HTTP 404, expected for a mod). This affected every HTTPS request Dolphin makes, not only covers.

### Covers are off by default on desktop Linux

`General.UseGameCovers` defaults to true only on Android (`UISettings.cpp`). In the port it is off until the user turns it on in Grid Options. Turning it on by default, as Android does, is a 3.8 default for the user to decide; it contacts GameTDB with each game's ID.

### The old `start()` saved the test backend

3.0's `start(path, backend)` used `Config::SetBaseOrCurrent`, which wrote the chosen backend into `Dolphin.ini`. Now:
- an explicit backend goes into the current-run layer, which is never saved;
- without one, the current-run override is cleared, and the core activates the configured backend at boot, per-game settings included.

The test profile still had `GFXBackend = Vulkan`.

### Smaller ones

- Image provider: QML's `sourceSize` with only a width asks for `305x0`. `QImage::scaled` to that returns a null image. The provider now scales by one side when the other is 0.
- Lomiri's `Page` already has a `pageStack` property; a page must not declare its own.
- `QSortFilterProxyModel::invalidateFilter()` is deprecated in Qt 6.10; `beginFilterChange()` / `endFilterChange()` instead.

## Not in 3.1

- Android menu entries that belong to later phases:
  - Settings → 3.6;
  - Open File → 3.7, with content-hub;
  - Install WAD, Load Wii System Menu, Import Wii Save, Import NAND Backup, Online System Update, NetPlay → deferred, as in the roadmap;
  - About → 3.6.
- Game properties entries:
  - Edit Game Settings, Clear Game Settings → 3.6;
  - Cheats, Convert, Start with Riivolution, System Update → deferred;
  - Set as Default ISO → 3.6.
- Watching folders for changes while the app is open: Android does not, and the rescan on return covers the usual case (copying over USB).

## Manual tests

Screen on, unlocked, rotation lock off:

```sh
adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 900'
```

**S3.1-M1. Library.**
- Does the library look like an Ubuntu Touch app, at a comfortable size? Upright and in landscape: 3 and 7 columns.
Yes.
- Switch tabs (Need for Speed in GameCube, Newer SMB in Wii). Is the tab remembered after restarting the app?
No. Will the tap ever be remembered if we launch the app from command line like this? 
- ⋮ → Grid Options: turn on "Download Game Covers from GameTDB". The Need for Speed cover should appear. Turn "Show Titles" off and on.
Yes. I copied Legend of Zelda Wind Waker in there, and the cover appeared as well. The 2D Wii Mario still had a blank cover. Maybe it's because it's a Taiwanese bootleg? 
- Search (magnifier): type part of a title.
Search works. 
- Pull the grid down to refresh, and use the Refresh button.
They both work. 

**S3.1-M2. Starting games.**
- Tap Need for Speed: it starts with the configured backend (Vulkan). Exit brings the library back.
Works as expected.
- Long press Newer SMB → Details: banner, title, facts. Back.
Works as expected.
- Long press → Start with OpenGL ES, then Exit.
Works as expected. 

**S3.1-M3. Folders.**
- ⋮ → Game Folders → +: browse to a folder (for example `Downloads`), tap ✓. It is listed, and the library rescans.
Works as expected.
- Swipe it right and remove it.
Works as expected.
- Optional: copy a game into `/home/phablet/dolphin-ut-games` while the app is in the background, then switch back. It should appear.
Works as expected.
Send the terminal output and the answers, and decide whether covers should be on by default (Android: on).
Please leave it on. 

(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 120'
running: ut: -u /home/phablet/dolphin-ut-user-w (for 120s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 120s
23:49:152 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
24:01:535 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
24:33:647 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
24:33:647 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (WaitForCommandBufferCompletion:286) vkWaitForFences failed:  (2: VK_TIMEOUT)
24:33:648 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
24:33:648 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (WaitForCommandBufferCompletion:286) vkWaitForFences failed:  (2: VK_TIMEOUT)
24:33:649 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
24:33:650 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
24:33:656 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
24:33:660 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
24:33:666 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
24:33:673 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
24:33:687 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
24:33:687 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (WaitForCommandBufferCompletion:286) vkWaitForFences failed:  (2: VK_TIMEOUT)
24:33:688 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
24:33:688 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (WaitForCommandBufferCompletion:286) vkWaitForFences failed:  (2: VK_TIMEOUT)
24:33:688 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
24:33:688 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (WaitForCommandBufferCompletion:286) vkWaitForFences failed:  (2: VK_TIMEOUT)
24:33:689 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
24:33:690 VideoBackends/Vulkan/VulkanLoader.cpp:245 E[Video]: (SubmitCommandBuffer:433) vkQueueSubmit failed:  (-4: VK_ERROR_DEVICE_LOST)
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 120'
adb: no devices/emulators found
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb devices
List of devices attached

(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 120'
running: ut: -u /home/phablet/dolphin-ut-user-w (for 120s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 120s
28:41:158 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
28:44:463 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
28:47:845 VideoBackends/OGL/OGLConfig.cpp:744 W[Video]: Missing OGL Extensions: PinnedMemory GSInstancing ClipControl DepthClamp 
28:48:095 VideoBackends/OGL/OGLConfig.cpp:744 W[Video]: Missing OGL Extensions: PinnedMemory GSInstancing ClipControl DepthClamp 
29:15:975 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
30:31:483 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 30'
running: ut: -u /home/phablet/dolphin-ut-user-w (for 30s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 30s
33:50:369 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 30'
running: ut: -u /home/phablet/dolphin-ut-user-w (for 30s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 30s
35:45:811 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 30'
running: ut: -u /home/phablet/dolphin-ut-user-w (for 30s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 30s
37:21:187 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell 'sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 30'
running: ut: -u /home/phablet/dolphin-ut-user-w (for 30s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 30s

One of the things I noticed was that Legend of Zelda Wind Waker doesn't work with Vulkan. I saw a lot of errors and no picture beyond the Nintendo logo. 
In comparison, the Android Dolphin on my other phone works fine with the same game on Vulkan. Is this a ubuntu touch issue, or a Dolphin issue, or our port issue?  


## Results of S3.1-M1…M3 (2026-11-15)

| Check | Result |
| --- | --- |
| Native look and size, upright and landscape | Yes |
| Tab remembered after a restart | **No**, fixed (below) |
| Grid options: covers, titles | Yes. Wind Waker (added by the user) got its cover too |
| Search, pull to refresh, Refresh | Yes |
| Tap to start, Exit back to the library | Yes |
| Details, Start with OpenGL ES | Yes |
| Add a folder with the browser, swipe to remove | Yes |
| A game copied in while the app is in the background shows up on return | Yes |
| **Covers on by default** | **User decision: yes, as on Android** (3.8) |

### The tab was not remembered

How the app is started does not matter here: the setting lives in `~/.config/Dolphin Emulator/dolphin-ut.conf` either way (a click will have its own `~/.config/<app id>/`). The value was saved, but always as 0. While the page starts, Lomiri's `Sections` reports its initial index 0, and the handler wrote that back before the saved value had been applied.

Fix:
- the page reads the saved value once it is complete (`settings.value("platformTab")`);
- the page only writes after that.

Checked over adb: with `platformTab=1` stored, the library opens on Wii (screenshot), and the file still says 1 afterwards.

### Newer Super Mario Bros.: no cover

It is not a bootleg. Newer Super Mario Bros. Wii is a fan-made mod with its own game ID, `SMNW03`:
- `W` is the region letter Dolphin reads as Taiwan, hence "Country: Taiwan" and the `JA` cover region;
- GameTDB has no cover for it (HTTP 404).

Dolphin also asks again on every scan, which DolphinQt does too: one failed request per game without a cover per refresh. Not worth changing now.

A custom cover works for such games: a PNG next to the game named `<game file name>.cover.png` (here `Newer Super Mario Bros. 1.3.0 [SMNW03].cover.png`), or `cover.png` in a folder that holds one game (`GameFile::CustomCoverChanged`). It takes priority over GameTDB's.

### Wind Waker hangs the GPU on Vulkan (stage 4)

The Legend of Zelda: The Wind Waker (PAL, `GZLP01`), Vulkan: the Nintendo logo shows, then thousands of `VK_ERROR_DEVICE_LOST` about 10 s in. OpenGL ES works.

What the tests show:
- **The GPU hangs in Qualcomm's own driver.** The kernel log shows `kgsl kgsl-3d0: MISC: GPU hang detected`, then `gpu fault ctx … ctx_type VK` from `dolphin-ut`, three times. Then `gpu fault threshold exceeded 3 faults in 3000 msecs`, and the driver drops the Vulkan context. This is a command-stream hang, not a memory (IOMMU) fault. The emulation itself keeps running at full speed.
- **Not the new frontend.** DolphinQt (`run-dolphin.sh vk-fs`), with no touch overlay or Lomiri code, hangs the same way.
- **Not Ubuntu Touch itself, as far as can be seen.** The kernel driver (KGSL) and the Vulkan driver are the phone's Android vendor blobs, used through libhybris. Presentation is the only part that differs from Android, and the hang is in rendering.
- **Not specialized shaders**: it hangs with ubershaders too (`ShaderCompilationMode` 1 and 2).
- **Not these options** (each tried alone, with the same hang about 10 s in):
  - `BackendMultithreading=False`;
  - EFB copies to RAM (`EFBToTextureEnable=False`);
  - skipping EFB access;
  - `DeferEFBCopies=False`;
  - XFB copies to RAM;
  - `DisableFog`.
- **Dolphin's Android-only Qualcomm workarounds are unlikely to explain it.** Dolphin detects the driver correctly (`DRIVER_QUALCOMM`), but three entries in `DriverDetails.cpp` apply only on Android, so they are off in the port:
  - `BUG_BROKEN_TEV_COLOR_ZERO` starts at driver version 700. This phone's driver is V@530 (built 2022-06-27), so it would be off on Android too;
  - `BUG_BROKEN_MULTITHREADED_SHADER_PRECOMPILATION` only matters with "wait for shaders before starting", which is off;
  - `BUG_SLOW_OPTIMAL_IMAGE_TO_BUFFER_COPY` is a speed workaround.

So the answer to "Ubuntu Touch, Dolphin or the port?": most likely **this phone's Qualcomm Vulkan driver (V@530)**, with something Wind Waker draws at its title screen. It is neither the port's frontend nor something Ubuntu Touch adds. The user's other phone has a different GPU or driver, and the Android app can also use Turnip (Mesa's open Adreno driver), so it is not a like-for-like comparison.

What would settle it:
- Android Dolphin on this same phone and driver. Waydroid is installed but not set up on stone-ut.
- Bisecting further in Dolphin's Vulkan feature use (dual-source blending, logic ops, framebuffer fetch, and so on). That means code changes in the Vulkan backend or `DriverDetails`; the user decides those (roadmap rule, stage 4 step 4).

Until then, a game like this runs on OpenGL ES, chosen per game once 3.6 adds per-game settings ("Start with OpenGL ES" in the long-press menu meanwhile).

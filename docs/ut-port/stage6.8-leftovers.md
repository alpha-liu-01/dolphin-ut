# Stage 6.8: smaller leftovers

Goal (`stage6-roadmap.md`), the smaller items left from earlier stages:
- driver zips through content-hub and a restart button on the GPU Driver page (both from stage 4, "Not done (later)");
- the Skylanders Portal and the Infinity Base;
- translations of the port's strings.

Status (2026-11-17): **done.** S6.8-M1–M4 passed; the in-game menu made scrollable with taller entries (S6.8-M5 passed), and buttons no longer light up while it scrolls (S6.8-M6 passed). **Translations moved to stage 7** (the user, below).

## Driver zips from other apps

- **Before:** Import Games and Share in the File Manager both end in `Storage::ImportFiles`, which skipped everything that was not a game ("not a game file"). A driver package downloaded in the browser could only be installed from the GPU Driver page's picker.
- **Now:** a zip with a `meta.json` (`GpuDrivers::IsDriverPackage`) is handed to the GPU Driver page's installer (`GpuDrivers::install`). The import runs on a worker thread, while the driver list belongs to the UI thread, so the installer runs there (`Storage::SetDriverInstaller`, a blocking queued call).
- **The result dialog** (Main.qml) says "GPU driver installed", or lists the driver beside imported games, with "choose it on Settings → Graphics Settings → GPU Driver" and an **Open GPU Driver** button. Choosing it stays a separate step, as on the page.
- Other files are still skipped as before.

## Restarting the app

- **Before:** after choosing another GPU driver, the page said to close Dolphin in the app switcher and open it again (stage 4: Lomiri tracks the app's process, and an in-place restart was untested). Import User Data (6.5) closed the app with the same advice.
- **Now** (`LomiriIntegration::restart`), when the app was started by Lomiri (it has an `APP_ID`):
  - A helper runs outside the app's own systemd unit (`systemd-run --user --collect`), since that unit's processes are killed when the app ends.
  - The helper waits for the app's process to end (up to 30 s), then runs `lomiri-app-launch APP_ID`.
  - Waiting matters: Dolphin is single-instance, so launching while the old process lives would only bring it forward.
- **The GPU Driver page:** a **Restart Dolphin** button when the chosen driver is not the one in use (not during a game). The status text says "Restart Dolphin to use …".
- **Import User Data:** "Restart Dolphin" instead of "Close Dolphin". It still ends without saving (6.5), so the imported settings stay.
- **Started from a terminal** (no app ID), both keep the old "close and reopen" text.

## Skylanders Portal and Infinity Base

- **Before:** the emulated portal and base could be turned on (Config → Wii: Skylanders Portal, Infinity Base), but there was no way to put figures on them.
- **Now,** as the Android app's in-game dialogs (`EmulationActivity.showSkylanderPortalSettings` / `showInfinityBaseSettings`, `jni/SkylanderConfig.cpp`, `jni/InfinityConfig.cpp`):
  - **The in-game menu** shows **Skylanders Portal** / **Infinity Base** for Wii games when they are enabled, as Android's menu does.
  - **The page:** each opens `FiguresPage.qml` over the paused game, as Settings does (6.1: `Emulator::openPageOverGame`); Back returns to the game.
  - **The slots:** eight Skylander slots, and the Infinity Base's nine positions (Play Set/Power Disc, Power Discs Two and Three, Player One/Two and their two abilities each). Each has:
    - **Load…:** a figure file from the picker (`.sky`, `.bin`, `.dmp`, `.dump`; `.bin` for Infinity).
    - **Create…:** a searchable list of the known figures (517 Skylanders, 104 Infinity, Dolphin's lists). The new file is saved in the user data, `Skylanders/` as DolphinQt does, and `Infinity/`, then loaded.
    - **Clear.**
- **The calls are Dolphin's, as the JNI makes them** (`Figures.{h,cpp}`):
  - `SkylanderPortal::LoadSkylander` / `RemoveSkylander` / `CalculateIDs`, and `SkylanderFigure::Create`;
  - `InfinityBase::LoadFigure` / `RemoveFigure` / `CreateFigure`.
- **When a game ends,** the figures are taken off the portal and base: they belong to `Core::System` and would otherwise stay into the next game. The slots empty.

## Translations: measured, not started

- **What exists:**
  - The port's texts are English. The generated settings use the Android app's English strings, and the pages have their own.
  - The Android app's translated `values-xx` resources are not in this repository (Dolphin's Android translations come from Transifex at release).
  - The translations in the tree are Dolphin's gettext catalogs (`Languages/po/*.po`, 30 languages), for DolphinQt and the core.
- **Measured** (exact text matches against `dolphin-emu.pot`, 3139 strings):
  - of the 384 different texts in the generated settings, **189 (49 %)** are in Dolphin's catalogs;
  - of the 222 item titles, 128 (57 %).
  - The rest are the Android app's own wording ("Dual Core (speedhack)", "Clear Log file", "Custom Top", …).
  - The port's own page and dialog texts are not in any catalog.
- **The options are the user's:**
  1. Use Dolphin's catalogs for what matches: half of the settings would be translated, so screens would be partly English. The port's pages stay English.
  2. Also mark the port's own texts for translation (`qsTr` and a template for translators), with no translations until someone writes them.
  3. Leave translations for now, until translators are found (as for the 6.7 tests).

## Test aids

- `main.cpp --test-restart-after MS` restarts as the button does. It only fires while the app runs in front; Lomiri stops an app that is not.
- `page-harness.qml`:
  - `import-files --test-arg PATH[,PATH…]` (as content-hub hands files over);
  - `figures --test-arg skylanders|infinity`.

## Checked over adb

Test build, test user folder (the installed driver and figure files removed afterwards):

| Check | Result |
| --- | --- |
| The Turnip package and a text file as from content-hub | the zip installed as "Mesa Turnip Driver v25.3.0-R11" (`drivers` in the result); the text file "not a game file"; the zip left where it was |
| Restart, the mechanism | the helper started with `systemd-run --user`, the dev app (started by `lomiri-app-launch dolphin-ut`, `APP_ID=dolphin-ut`) ended; the helper saw it go and Lomiri started a new instance (new PID) |
| Restart, from the app | not checkable over adb: Lomiri stops (SIGSTOP) an app that is not in front, so the test timer did not run with the phone locked. M2 |
| Skylanders | 517 known; "Air Hourglass" created (`Skylanders/Air Hourglass.sky`, 1024 bytes) and on slot 1; cleared; loaded from its file into slot 2; `/etc/hostname` → "Failed to open the file." |
| Infinity | 104 known; a figure created (`Infinity/…bin`, 320 bytes) and on Player One; cleared; loaded back; a bad file refused |
| The pages | the eight Skylander slots and the nine Infinity positions with their figures |

## Manual tests

Screen on, unlocked. **M1 and M2 need the app started from the app drawer** ("Dolphin (dev)", installed in 3.7 by `dev-install.sh`), so that it has an app ID. It uses Dolphin's default user folder, not the test folder.

**S6.8-M1. A driver zip from the File Manager.** File Manager → Downloads → `Turnip_v25.3.0_R11.zip` → Share (or press and hold → Share) → Dolphin (dev).
- Does Dolphin say "GPU driver installed" with the driver's name and an **Open GPU Driver** button?
- Does the button show the driver in the list?

**S6.8-M2. Restart.** On the GPU Driver page, choose Turnip.
- Does **Restart Dolphin** appear? Tap it: does Dolphin close and come back by itself within a few seconds?
- Does the page then say "In use now: Turnip …"?
- Choose "System driver", Restart Dolphin again: back on the system driver?

**S6.8-M3. Skylanders Portal.** Settings → Config → Wii → Skylanders Portal on. Start Newer Super Mario Bros. (any Wii game; it does not use the portal). Menu:
- Is there **Skylanders Portal**? Tap it: does the page come up over the paused game?
- Tap Skylander 1 → Create… → search "spyro" → pick one: is it on slot 1? Back: does the game resume?
- Turn the portal setting off again afterwards.

**S6.8-M4. Infinity Base.** The same with Config → Wii → Infinity Base and the menu's **Infinity Base** (create a figure on Player One).

## Results

2026-11-16, the user: "All tests passed, but the only issue I noticed was that there are too many items in the in-game menu and some of the items are unreachable. Please make this menu scrollable and each item taller."

| Test | Result |
| --- | --- |
| M1 Driver zip from the File Manager | Pass |
| M2 Restart | Pass |
| M3 Skylanders Portal | Pass |
| M4 Infinity Base | Pass |
| The in-game menu | **Too many entries:** with Settings (6.1) and Skylanders Portal / Infinity Base (here), the last ones fell below the screen and could not be reached |

### The in-game menu: scrolling and taller entries (2026-11-17)

- **Why it was cut off:** stage 3.3/3.4 sized the menus to fit, as ImGui scrolls only with a wheel or its scrollbar, which is too thin for a finger. Wii games now have up to 15 entries, so the bottom ones fell off.
- **Scrolling (`TouchOverlay::TouchScroll`):**
  - In both menus (the in-game panel and Overlay Controls), a finger that moves vertically by more than ~0.4 of a text line scrolls the menu, following the finger.
  - The press is taken from the button under it (`ImGui::ClearActiveID`), so lifting the finger after a scroll does not click.
  - A thin bar on the right edge shows that there is more, and where.
- **Taller entries:**
  - Button padding is up from 0.32–0.35 to 0.6 of a text line, and spacing from 0.3–0.35 to 0.45: each entry is about 2.2 lines tall instead of 1.65.
  - The in-game panel's text is the overlay menu's size (4.5 % of the short side, was 3.8 %), as it no longer needs to shrink to fit.
- **Checked:** a screen capture (Newer, portrait, every entry on) shows the taller entries running past the screen's bottom, which is what the scrolling is for. Scrolling itself needs a finger: the re-test below.

**S6.8-M5 (re-test). The in-game menu.** Enable Skylanders Portal and Infinity Base (Config → Wii), start Newer, open the menu, in landscape and in portrait:
- Are the entries easy to hit?
- Drag the list up: does it scroll, with a bar on the right, and does lifting the finger after a drag leave the entry under it untouched?
- Can you reach and use the last entries (Change Disc, Reset Emulation, Exit Emulation)?
- Overlay Controls → its pages (Toggle Controls is the longest): the same.
 

### S6.8-M5 re-test results (2026-11-17)

- **Pass, all four points:**
  - the entries are easy to hit;
  - dragging scrolls with the bar, and lifting the finger does not click;
  - the last entries can be reached;
  - Overlay Controls' pages scroll the same way.
- **One issue:** while scrolling, the buttons the finger passed over still **lit up** (without being triggered). That was ImGui's hover colour: the finger stays down while the list moves under it, so each button it is over counts as hovered.
- **Fix** (`TouchOverlay::PushTouchScrollColors`): while a drag scrolls a menu, the hovered and pressed colours of buttons, list headers, check boxes, sliders and their grabs are set to the normal ones (pushed when the window begins, popped before `TouchScroll`).
  - A tap still shows the pressed colour as before, and the moment before the drag passes its threshold is unchanged.
  - Checked over adb: a game run with the menu open, no ImGui errors, a normal stop.

**S6.8-M6 (re-test).** In the in-game menu and in Overlay Controls, drag the list slowly up and down over the buttons: do they stay unlit while it scrolls? Does a plain tap still light the button and act?

### S6.8-M6 re-test result (2026-11-17)

Pass: the buttons no longer light up while a menu scrolls.

### Translations: moved to stage 7 (2026-11-17, the user)

The user's decision: translations are left out of stage 6 and go to **stage 7** (an extension of stage 5, the release), and some of that work comes after the first release. The measurement and the three options above are its starting point.


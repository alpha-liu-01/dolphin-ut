# Stage 6: feature completeness

Goal (`roadmap.md`, stage 6): the Android app's remaining features, in phases ordered by what a player meets first. Stages 3–5 left a playable, installable app; this stage fills in what was deferred.

Rules as before:
- The port changes the frontend (`Source/Core/DolphinUT/`, `ut/`), not how the emulated consoles behave.
- Features use Dolphin's existing core paths, as the Android app and DolphinQt do.
- Anything that changes emulated behavior is the user's decision and change.

## Phases

| Phase | What | Size | Why this order |
| --- | --- | --- | --- |
| **6.1** | **Settings during a game, and answers to Dolphin's questions** (the 3.6 second pass): Settings in the in-game menu; settings that cannot change while a game runs shown locked, as on Android; a Yes/No dialog for Dolphin's questions instead of "log, show, continue"; Dolphin's errors shown when no game runs | medium | Met in every session. The alert dialog fixes a real gap (3.6: an assertion's question could not be answered) |
| 6.2 | **The remaining settings:** Hotkeys (the mapping page, for Dolphin's hotkeys), Post-Processing Effect (the shader list), the log settings (verbosity, log types, clear), Set Custom RTC, the two debug actions. Android-only items (themes, statistics identity) and the real Balance Board stay out | small–medium | Closes the generator's "skipped" list (19 items) |
| 6.3 | **Wii file actions:** Install WAD, Import Wii Save, Import BootMii NAND Backup, Load Wii System Menu, Online System Update | small–medium | The file picker exists (3.7); Wii users need the System Menu and WADs |
| 6.4 | **Per-game tools:** cheats (AR and Gecko codes), Riivolution patches, disc conversion (RVZ and others), as in the Android app's game properties | medium | Common requests; all are existing core features with Android screens to follow |
| 6.5 | **Export and user data:** save states and memory cards through content-hub; the zip import/export of the whole user folder (Android's User Data) | medium | Backups and moving to another phone |
| 6.6 | **Wii Remote motion from the phone's sensors** (the Android overlay's motion controls), if the sensors are reachable (sensorfw / Qt Sensors) | large | Many Wii games need pointing or shaking. Needs a probe first |
| 6.7 | **RetroAchievements:** log in and out, enable, hardcore; notifications in the picture | medium | Built into Dolphin (`USE_RETRO_ACHIEVEMENTS` is on in this build) |
| 6.8 | Smaller leftovers: driver zips through content-hub and a restart button on the GPU Driver page (stage 4); Skylanders and Infinity Base; translations of the port's strings | small each | |
| — | Not planned (not in the Android app, or no use on a phone): NetPlay, GBA link, a TV layout. Feeding Ubuntu Touch findings back to #14962 is the user's (reports on the PR, not agent pull requests) | | |

A phase stops for on-screen tests, as in stage 3. The order can change: say which feature matters most.

## 6.1 Settings during a game, and Dolphin's questions

#### Status (2026-11-16): done (all on-screen tests passed, M3 after the fix)

Notes: `stage6.1-ingame-settings.md`.
- **Settings in the in-game menu** (after Load State, as on Android). The game pauses, the library window comes up over it with Settings, and Back returns to the game and resumes it. The game window stays mapped, since its surface is the renderer's.
- **Locked while a game runs:** the generator carries the Android app's `NOT_RUNTIME_EDITABLE` lists and the SYSCONF rule into `settings.json` (33 settings). They are greyed out during a game, with Android's note, and the model refuses them.
- **Dolphin's alerts and questions** get a dialog (Yes/No or OK, plus "Ignore for this session" for warnings), over the game if one runs. The asking thread waits, with DolphinQt's CPU/GPU-thread safeguard; the game is not paused for a question, so a blocked GPU thread cannot deadlock the pause.
- **Checked over adb:** questions and warnings from a worker thread; settings over a running game and back at 100% speed; a warning during a game; locked items on screenshots; a boot error raised on the UI thread.
- **S6.1-M3 failed:** a question during a game could stay behind it. Lomiri has no xdg-activation, so a window shown without a recent tap need not come forward. Questions during a game are now drawn in the game picture (ImGui). From the CPU thread, redraws are pushed while it waits. From the thread that draws the game, the 3.6 on-screen message is used and the game continues.

## 6.2 The remaining settings

#### Status (2026-11-16): done (all on-screen tests passed)

Notes: `stage6.2-remaining-settings.md`. The generator's "skipped" list is down from 19 to 9, all deliberate (Android-only items, achievements for 6.7, the real Balance Board).
- **Hotkeys:** the mapping page over Dolphin's hotkey controller, with the hotkeys the Android app handles. The Android app's `HotkeyScheduler`, minus its JNI, runs them during a game. None are mapped on a first start (as on Android), so the hotkey thread is idle until one is.
- **Post-Processing Effect** (Dolphin's shader list), **Set Custom RTC** (date and time pickers, UTC), the **log** menu (verbosity, 58 log types, enable/disable all, clear), and the two **JIT profiling** actions.
- **Fixed on the way:**
  - "Write Logs to File" (3.6) never took effect: Dolphin's `LogManager` wrote its old state back at exit. Log settings now reach the `LogManager`.
  - Input detection on the mapping pages did not work with the game paused under Settings (6.1).

## 6.3 Wii file actions

#### Status (2026-11-16): done (S6.3-M1–M4 passed; M5 skipped, no files). A GameCube disc started from the Wii Menu crashes in Dolphin's core (MIOS boot with no IOS kernel set): the user's decision, see the notes

Notes: `stage6.3-wii-files.md`.
- **Library ⋮ → Wii System:** Load Wii System Menu (with the installed version), Perform Online System Update (it installs the Wii Menu first if it is missing, with a region choice), Install WAD, Import Wii Save, Import BootMii NAND Backup.
- **Perform System Update** on a Wii disc's long press.
- Dolphin's own code runs on a worker thread, with the Android app's texts. DolphinQt's extras are added where Android lacks them: the NAND import's progress, cancel and keys-file question, and the certificates after an update.
- The Wii Menu boots as a NAND title (`Emulator::startSystemMenu`).

## 6.4 Per-game tools

#### Status (2026-11-16): done (S6.4-M1–M5 passed)

Notes: `stage6.4-game-tools.md`. The library's long press gains the Android game properties' Start with Riivolution Patches, Set as Default ISO, Convert File and Edit Cheats, in Android's order.
- **Cheats:** patches, AR and Gecko codes, graphics mods; add, edit, delete, enable; Download Gecko Codes; Android's warnings when cheats or graphics mods are off.
- **Riivolution:** the patch XMLs for the game with their choices, saved, then a start with the patches.
- **Convert File:** ISO, GCZ, WIA or RVZ with Android's options and warnings, progress and cancel. Need for Speed (1.46 GB) to RVZ took 17 s on the phone.

## 6.5 Export and user data

#### Status (2026-11-16): built and checked over adb; on-screen tests S6.5-M1–M5 pending

Notes: `stage6.5-user-data.md`. Config → User Data gains Android's actions:
- Export User Data: a zip in Documents/Dolphin/Backups, visible over USB.
- Import User Data: every entry is checked before the folder is emptied, and the app closes without saving afterwards.
- Reset Dolphin Settings: the port's defaults come back at once.

It also gains single files out (to the File Manager or another app through content-hub) and in (a save state or memory card copied into a folder of the user data), since the user folder is not visible over USB.


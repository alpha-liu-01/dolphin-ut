# Stage 6.1: settings during a game, and Dolphin's questions

Goal (`stage6-roadmap.md`): the 3.6 second pass.
- **Settings** in the in-game menu, as in the Android app.
- The settings that cannot change while a game runs shown **locked**.
- Dolphin's alerts and questions **answered in a dialog**, instead of "log, show on screen, continue".

Status (2026-11-16): **done.** S6.1-M1, M2 and M4 passed. M3 failed at first (a question during a game could stay behind it); questions during a game are now drawn in the game picture, and the re-test passed.

## How it works

### Settings from the in-game menu

The settings pages live in the library window, which is hidden while a game runs: hiding it is what lets Lomiri give the game fullscreen (3.2). The two ways left open in 3.6 were to show that window over the game, or to draw the settings into the picture like the menu. The first reuses all 206 settings items, the game layer and the pickers, so it was chosen.

- **The entry:** "Settings" in the in-game menu, after Load State, the Android app's place (`fragment_ingame_menu.xml`). It opens the top-level Settings, as the Android app's `MENU_ACTION_SETTINGS` does (global settings; game settings stay in the library's long press).
- **Opening:** `Emulator::openSettingsOverGame` pauses a running game, then shows, raises and activates the library window.
  - The game window stays mapped: its Wayland surface is the video backend's, and hiding the window could take the surface away from it. Lomiri stacks the app's windows, so the library window covers the game.
  - QML pushes the Settings page with "The game is paused; Back returns to it."
- **Returning:** leaving that page (its Back) calls `returnToGame`. It hides the library window, activates the game window (whose fullscreen check from stage 5 runs again), and resumes the game if it was running before.
- **Live changes:** values change through Dolphin's config, as in the library: settings the core reads live (graphics, speed limit, …) apply on return.
- **Shared mechanism:** `BeginOverGame`/`EndOverGame` is counted, so the settings and a question can overlap. A game that ends meanwhile resets it.

### Locked while a game runs

The Android app's rule (`SettingsItem.isEditable`): during a game, a setting is editable only if it is not on its type's `NOT_RUNTIME_EDITABLE` list (`BooleanSetting.kt`, `IntSetting.kt`, `StringSetting.kt`) and is not a SYSCONF (Wii system) setting.
- Dolphin's `Config::IsSettingSaveable` excludes only the controller systems, which these menus do not show.
- **The generator** (`ut/tools/gen-settings.py`) reads those lists and marks 33 settings `lockedWhileRunning` in `settings.json`, among them:
  - Dual Core, CPU core, MMU, fastmem, cheats, region and language;
  - the memory card slots and the serial port;
  - the video backend;
  - the Wii system settings.
  - The virtual DSP engine and MEM1/MEM2 sizes take the lock of the settings they stand for. CPU Boost stays editable: the app applies it when the game resumes.
- **The page:** during a game, locked items are greyed out and do not respond. Pages that have any say at the top "Greyed-out settings can't be changed while a game is running." (Android's `setting_not_runtime_editable` text, which it shows as a toast). `SettingsModel::setValue` and `clear` refuse them too.

### Dolphin's alerts and questions

As DolphinQt's `QtMsgAlertHandler` and the Android app's `AlertMessage`:
- **The dialog:** a Lomiri dialog with the alert's caption and text. Questions get Yes/No, other alerts OK. Warnings also get **"Ignore for this session"**, which sets `MAIN_USE_PANIC_HANDLERS` off for the current run, so Dolphin stops showing alerts until the app restarts.
- **The asking thread waits for the answer.** From another thread, the call is queued to the UI thread and blocks (`BlockingQueuedConnection`). On the UI thread (a boot error inside `BootCore`), a nested event loop waits, as a modal dialog's would.
- **Deadlock safeguards:**
  - As DolphinQt does, while the UI thread asks for the CPU or GPU thread it declares itself that thread (`Core::DeclareAsCPUThread`/`GPUThread`).
  - **The game is not paused for a question.** The asking thread may be the GPU thread, and pausing the core from the UI thread could wait for it. The asking thread is blocked anyway.
- **Where it shows** (revised after S6.1-M3, below):
  - **No game, a game starting, or the settings open over a game:** in the library window.
  - **A game on screen:** in the game picture (ImGui, `TouchOverlay::ShowQuestion`), over everything, answered by touch. If the CPU thread asked (dual core), the game presents nothing while it waits, so the app pushes redraws through the paused-redraw path. This works because the UI thread is declared the CPU thread meanwhile, so `Core::RunOnCPUThread` runs them inline instead of waiting for the blocked CPU thread.
  - **The thread that draws the game asked** (the GPU thread in dual core, the CPU thread in single core): nothing can be drawn while it waits. As in 3.6, the message goes on screen (OSD) and the game continues: OK, or Yes to a question, which is Dolphin's "Ignore and continue". Newer SMB's "GFX FIFO: Unknown Opcode" comes from that thread.
- **What 3.6 did instead** (log, show on screen, continue) is gone. The stage 4 filter still drops the thousands of repeats after a lost GPU, before any dialog.
- **Answering No to an assertion's "Ignore and continue?"** stops the app, as upstream does on every platform.
- **Quitting the app** while a dialog is open (SIGTERM) ends the wait as No / OK.

### Test aids (adb)

- `--settings-after MS`, `--return-after MS`: open the in-game Settings, and return to the game.
- `--test-alert KIND@MS`: an alert with the answer logged. KIND `question`, `warning`, `info` come from a worker thread; `cpu-question` from a job on the CPU thread; `gpu-warning` from an event on the GPU thread.
- `--answer-after MS:BUTTON`: answer the newest dialog (0 OK/No, 1 Yes, 2 Ignore for this session).

## Checked over adb

| Check | Result |
| --- | --- |
| Question from a worker thread, no game, answered Yes | Dialog "Question / Test question … Continue?" with Yes and No (window screenshot); the thread got `true` |
| Warning, answered "Ignore for this session" | The thread got `true`; panic handlers off for the run |
| Need for Speed from save state 1: Settings after 9 s, return after 15 s | `library over the game: settings` → `core: paused` → closed → `core: running`, 100% speed right after |
| A warning during the game | Library over the game, answered OK, the game window back to fullscreen |
| Config → General during a game (window screenshot) | Note at the top. Dual Core, Enable Cheats, Mismatched Region and Fallback Region greyed out; CPU Boost, Change Discs Automatically, Speed Limit and Enable Savestates editable |
| A missing game file (`BootCore` on the UI thread) | The dialog over the game window ("The specified file … does not exist"), answered, then back to the library |

Not checked over adb: how it looks on the real screen (the phone was locked, so `mirscreencast` only sees the lock screen) and the window stacking with a touch. Those are the manual tests.

## Manual tests

Screen on, unlocked, from an interactive `adb shell`. `run-dolphin.sh` runs the test build (`/home/phablet/dolphin-ut-arm64`, already deployed); the installed click does not have 6.1 yet.

**S6.1-M1. Settings from the in-game menu.**
`GAME="/home/phablet/Documents/Dolphin/Games/Need for Speed - Most Wanted.iso" sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut-start 600`
- In a race, Menu → **Settings**. Does the settings window come up over the game, with the game paused behind it (audio stops)? Yes. 
  - Which way round, and how big, is it: portrait or landscape, top panel or fullscreen? 
  The same orientation as the game, with top panel.
- Graphics → Statistics (or the Interface page) → **Show FPS** on, then Back until the game returns. Does the game resume where it was, fullscreen, with the FPS counter showing?
Yes. 
- Once more: Settings, Back straight away. Does the game come back fullscreen each time?
Yes. 

**S6.1-M2. Locked settings.** Menu → Settings → Config → General during the game.
- Is "Dual Core" greyed out, with the note at the top, and does tapping it do nothing?
Yes. 
- Is "Speed Limit" still adjustable, and does a change apply when you return to the game?
Yes. 

**S6.1-M3. A question during a game.**
`GAME="/home/phablet/Documents/Dolphin/Games/Need for Speed - Most Wanted.iso" sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut-start 300 --test-alert question@20000`
About 20 s after the start a dialog asks "Test question from a worker thread. Continue?".
- Does it appear over the game? Tap **Yes**: does the game come back and keep running?
This one didn't appear over the game. I had to switch to the game library in recent apps to see it. 
- Then with `--test-alert warning@20000`: are there OK and "Ignore for this session" buttons? Tap OK.
This one did interrupt the game but it automatically switched to the game library(and showed the warning) instead of showing it over the game. 

**S6.1-M4. Switching apps with the settings open.** In-game Settings open, switch to another app, then back to Dolphin.
- Are the settings still there? Yes. 
- Does Back return to the game, which then resumes? Yes. 

## Results

| Test | Result |
| --- | --- |
| S6.1-M1 Settings from the in-game menu | **Pass.** The settings come up over the paused game, in the game's orientation, with the top panel. Show FPS applied on return, and the game came back fullscreen each time |
| S6.1-M2 Locked settings | **Pass.** Dual Core greyed out and unresponsive, with the note; Speed Limit adjustable and applied |
| S6.1-M3 A question during a game | **Fail, then pass after the fix.** The question did not come over the game: it was found through the app switcher. The warning did come forward, but as the library window with the dialog, which looked like leaving the game. With questions drawn in the picture, the re-test passed (below) |
| S6.1-M4 Switching apps with the settings open | **Pass.** The settings were still there, and Back resumed the game |

### S6.1-M3: why the dialog stayed behind the game

- **Bringing a window forward is up to Lomiri.** The compositor offers no `xdg_activation_v1` (its globals: `wl_compositor`, `wl_shell`, `xdg_wm_base` v1, `zxdg_shell_v6`, `qt_windowmanager`, `android_wlegl`, …; `WAYLAND_DEBUG` run). Qt's `requestActivate()` and `raise()` therefore send nothing, and whether the re-shown library window comes to the front is Lomiri's choice.
- **The settings always worked** because they open right after a tap on the game. A question arrives without one: in the warning run you were probably touching the screen (racing), in the question run not. The two were otherwise the same code.
- **Fix: during a game, questions are drawn in the game picture,** as the in-game menu is, so no window has to come forward. The three routes are under "Where it shows" above. The library window is used only when no game is on screen, or when the settings already cover the game.

**Checked over adb** (Need for Speed from save state 1; the phone was locked, so no screen captures):

| Asked from | Result |
| --- | --- |
| A worker thread (question, answered Yes) | "question 1 in the game picture", the thread got Yes; 100% → 98% speed around it |
| The CPU thread (question, answered Yes after 5 s) | In the picture; 156 redraws pushed in those 5 s (about 31 a second); the thread got Yes and the game continued at full speed |
| The GPU thread (warning) | "shown on screen, continuing" at once; no stall, 100% speed |

**S6.1-M3 (re-test).** Screen on, unlocked. Each command starts Need for Speed; the dialog comes about 20 s in, so you can keep racing or let go of the screen; it should show either way.
1. `GAME="/home/phablet/Documents/Dolphin/Games/Need for Speed - Most Wanted.iso" sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut-start 120 --test-alert question@20000`
   - Does the question appear in the middle of the game picture, with Yes and No? Yes.  Does the game keep running behind it? No. It paused. 
   - Tap Yes: does it go away? Yes, and the game continued. 
2. The same with `--test-alert cpu-question@20000`. The game picture should freeze behind the dialog: the emulated CPU waits for the answer. Tap Yes: does the game continue? The same as (1)
3. The same with `--test-alert warning@20000`. Are there OK and "Ignore for this session"? Tap "Ignore for this session". Yes, and the game continued running behind the warning.
4. The same with `--test-alert gpu-warning@20000`. No dialog: a red message at the top left for a few seconds, and the game keeps going. Just like you described. 
5. Optional, a question with no game: `sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 60 --test-alert question@5000`. A dialog in the library; tap No. Works as expected. 

### S6.1-M3 re-test results

| Step | Result |
| --- | --- |
| 1. Question from a worker thread | In the middle of the picture with Yes and No; Yes closed it and the game continued. You saw the game pause behind it (see below) |
| 2. Question from the CPU thread | The same; the picture froze behind it as expected, and Yes continued the game |
| 3. Warning | OK and "Ignore for this session"; the game kept running behind it |
| 4. Warning from the GPU thread | No dialog: the red message at the top left, and the game kept going |
| 5. Question with no game | The dialog in the library; No worked |

**Step 1 "paused".** Steps 1 and 3 take the same route: a worker thread asks, and the game is not paused. Re-run over adb, both open for 9 s without an answer:

| | During the dialog | After |
| --- | --- | --- |
| Question (step 1) | 99%, 92% speed (48.5, 47.0 VI/s) | 85% |
| Warning (step 3) | 99%, 91% speed (46.8, 45.0 VI/s) | 85% |

The emulation runs behind both, as with DolphinQt and the Android app, where only the asking thread waits.
- **What likely looked like a pause:** a dialog releases the touch controls and takes the touches, so in a race the car stops accelerating.
- **Why the app does not pause for it:**
  - Dolphin's own alerts during a game come almost always from the CPU thread, which freezes the game anyway (step 2), or from the GPU thread (step 4).
  - Pausing the core for a question from another thread (audio, disc, IOS) could deadlock: the CPU thread might be waiting for exactly that thread.

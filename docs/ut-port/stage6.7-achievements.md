# Stage 6.7: RetroAchievements

Goal (`stage6-roadmap.md`): RetroAchievements as in the Android app:
- turning achievements on;
- logging in and out;
- hardcore mode;
- notifications in the picture.

Dolphin builds them in (`USE_RETRO_ACHIEVEMENTS` is on in this build), but the port never started Dolphin's `AchievementManager`, and its settings menu was hidden.

Status (2026-11-16): **built and checked over adb (the menu, turning on and off, the login dialog, hardcore active); on-screen tests pending** (below). A real login needs the user's RetroAchievements account, so it is left for the manual tests: nothing was sent to retroachievements.org from here.

## How it works

The achievements, their notifications and hardcore mode's rules are Dolphin's own (`Core/AchievementManager`, rcheevos, `VideoCommon/OnScreenUI`). The port does what the Android app's frontend does:
- starts and stops the manager;
- shows the settings;
- logs in and out;
- greys out what hardcore forbids.

### Starting Dolphin's AchievementManager (`Achievements.{h,cpp}`, `achievements` in QML)

- **At start-up:** `AchievementManager::Init` runs with the app, as in `MainAndroid`'s `Initialize` and DolphinQt's `MainWindow`.
  - It does nothing unless achievements are enabled.
  - If a login token is stored, it logs in with it.
  - `Shutdown` runs at exit, before Dolphin's config goes.
- **On the switch:** "Enable Achievements" starts or stops the manager, as the presenter's `achievementsEnabledSetting` does (`AchievementModel.init` / `shutdown`). `SettingsModel` gained a `valueChanged` signal for this. The switch is locked during a game, from Android's `NOT_RUNTIME_EDITABLE`.
- **Logging in,** as `LoginDialog` and `AchievementModel.login`:
  - the username is stored (`RA_USERNAME`), and the password goes to `AchievementManager::Login` only;
  - the result comes from `login_event`;
  - on success Dolphin keeps the token it got back (`RA_API_TOKEN`) and the port saves the settings, since the core sets the token without saving it;
  - on failure the dialog says "Login Failed: " with rcheevos' error text.
- **Logging out:** `AchievementManager::Logout` (the token cleared), then saved.
- **The account:** display name and score come from `GetPlayerDisplayName` / `GetPlayerScore` once the server answers. The item refreshes on the manager's `update_event`.

### The settings menu (generator)

Config → **RetroAchievements**, which was hidden, is now generated from the presenter's `addAchievementSettings`:
- **Enable Achievements:** the presenter's converter object around `ACHIEVEMENTS_ENABLED`, read as that setting.
- **Log In / Log Out:** one item, `achievementsAccount` in `SettingsPage.qml`. Logged out, it shows "Log In" and opens the login dialog (username, password, Log In, Cancel; "Logging In…"). Logged in, it shows "Log Out" with "Logged in as NAME (N points)" and asks before logging out.
- **Dolphin's switches,** as Android's:
  - Enable Hardcore Mode (on by default), Enable Unofficial Achievements, Enable Encore Mode, Enable Spectator Mode;
  - Show Leaderboard Tracker, Show Challenge Indicators, Enable Progress Notifications, Play Sounds for Notifications.
  - The first four are locked during a game, as on Android.
- **Visibility:** as the presenter, everything after "Enable Achievements" shows only while it is on. The generator marks those items `visibleIf`, and `SettingsPage` hides them.
- **Global only:** the menu is global, as the log is (6.2). Achievements are not a game setting.
- **The skipped list** is now 6 items: Android's theme and statistics items, and the real Balance Board.

### Hardcore mode

- **Dolphin enforces the rules itself** (`IsHardcoreModeActive`):
  - no loading of save states (`State.cpp`: "Loading savestates is disabled in RetroAchievements hardcore mode");
  - no speed below 100 % (`CoreTiming.cpp`);
  - no cheats or Riivolution patches;
  - no debugging.
- **The port's in-game menu greys out Quick Load and Load State** while hardcore is active, as the Android app strikes them through (`MenuFragment`). Saving stays.
- **It is active as soon as achievements are on with hardcore enabled** (Dolphin's default), even before logging in or loading a game (`IsHardcoreModeActive`: no game info → true). That is Dolphin's rule on every platform. Turning achievements on therefore turns state loading off until hardcore is switched off.

### Notifications in the picture

Dolphin's `OnScreenUI` draws them with ImGui into the game picture, which the port's presenter already shows:
- the game's achievement summary at start;
- unlocks with their badges;
- the challenge indicators and the leaderboard tracker.

The sounds (`Sys/Sounds`, in the click) play through Dolphin's audio thread.

### Test aids

`page-harness.qml`: `achievements --test-arg on|off` (sets Enable Achievements, prints the state, opens the menu). `settings-tap:CONFIG_ACHIEVEMENTS --test-arg "Log In"` opens the login dialog.

## Checked over adb

Test build, test user folder:

| Check | Result |
| --- | --- |
| Start-up | the manager initialised; with achievements off, nothing else happens |
| Enable Achievements on | "achievements: on" (the manager started); hardcore active at once (`hardcoreActive` true), not logged in; `RetroAchievements.ini`: `Enabled = True` |
| The menu | Enable Achievements, Log In ("A RetroAchievements account"), and Dolphin's eight switches with their defaults (hardcore, leaderboard tracker, challenge indicators, sounds on) |
| The login dialog | username, password (hidden), Log In (enabled once both are filled), Cancel |
| A game with achievements on | started; the boot's `--state-slot 1` load request went to the core, which refuses it in hardcore with an on-screen message (not in the log; M2 checks it on screen) |
| Enable Achievements off | "achievements: off" (the manager stopped); hardcore off; `Enabled = False` (the test folder left with achievements off) |
| Generator | Config → RetroAchievements present (global only); 6 items skipped, all deliberate |

## Manual tests

These need a **RetroAchievements account** (free at retroachievements.org); without one, M1's failed login and M2 can still be done. Screen on, unlocked, on Wi-Fi, from an interactive `adb shell`:

`sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 1800`

**S6.7-M1. Turn on and log in.** Library ⋮ → Settings → Config → **RetroAchievements**.
- Only "Enable Achievements" shows at first? Turn it on: do Log In and the eight switches appear?
- Log In with a wrong password: "Login Failed: …" in the dialog?
- Log In with your account: does the dialog close, and does the item then say "Logged in as NAME (N points)"?
- Quit and start Dolphin again: still logged in (the token was kept)?

**S6.7-M2. Hardcore in a game.** With Enable Hardcore Mode on (the default), start Need for Speed. Menu:
- Are **Quick Load** and **Load State** greyed out, while Save State works?
- (Optional) Menu → Settings → Config → General → Speed Limit below 100 %: does the game ignore it (it stays at full speed)?

**S6.7-M3. Achievements in the picture.** Logged in, start a game RetroAchievements knows. Need for Speed: Most Wanted and The Wind Waker have achievement sets; your copies must match the set's hashes.
- At the start: does a notification in the picture show the game and its achievements ("You have X of Y achievements unlocked" or similar)? Note the text.
- If an achievement is easy to get early, does its unlock show with a badge and a sound?
- If the game is not recognised, what does the notification say?

**S6.7-M4. Log out and off.** Settings → RetroAchievements → Log Out → confirm: "Log In" again? Then Enable Achievements off: do the other items disappear, and are Quick Load and Load State back in a game?

## Results

(to be filled in)

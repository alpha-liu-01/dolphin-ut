# Stage 6.4: per-game tools

Goal (`stage6-roadmap.md`): the Android app's game properties that the port lacked:
- Edit Cheats (patches, AR codes, Gecko codes, graphics mods);
- Start with Riivolution Patches;
- Convert File (ISO, GCZ, WIA, RVZ);
- Set as Default ISO, a one-liner on the way.

Status (2026-11-16): **done.** Checked over adb (all three tools, end to end) and on screen (S6.4-M1–M5).

## How it works

Everything runs Dolphin's own code, as the Android app's JNI does: `ActionReplay`, `Gecko`, `PatchEngine`, `GraphicsModGroupConfig`, `DiscIO::Riivolution`, `AddRiivolutionPatches`, and `DiscIO::ConvertTo*`. The port adds pages and threads. Cheats and Riivolution patches change the running game only as Dolphin's existing systems do, with the user's choice. Nothing in the emulated consoles' behavior is changed.

### Where it is

The library's long press, in the Android game properties' order:
1. Details
2. **Start with Riivolution Patches** (discs)
3. **Set as Default ISO** (discs)
4. **Convert File** (where `GameFile::ShouldAllowConversion`)
5. Perform System Update (Wii discs, 6.3)
6. Edit Game Settings
7. **Edit Cheats**
8. Clear Game Settings
9. the port's Start with Vulkan / OpenGL ES

`GameList::details` gained `disc`, `convertible`, `gametdbId` and `discNumber` for these.

### Edit Cheats (`Cheats.{h,cpp}` as `cheats`; `CheatsPage.qml`)

- **Loading,** as the JNI's `loadCodes`:
  - the shipped codes come from the default game INIs (`SConfig::LoadDefaultGameIni`);
  - the user's from `GameSettings/<ID>.ini` (always that file, as on Android);
  - patches are the `OnFrame` section;
  - graphics mods are `GraphicsModGroupConfig`, filtered as `GraphicsModGroup.getMods` (only mods with a feature for a target group).
- **The list,** as `CheatsAdapter`:
  - Patches, AR Codes, Gecko Codes and Graphics Mods (the last only if the game has some), each code with a switch;
  - "Add New Patch / AR Code / Gecko Code" at the end of each section;
  - "Download Gecko Codes" under the Gecko section.
  - At the top, Android's warnings when the cheat system ("Dolphin's cheat system is currently disabled.") or graphics mods are off, each with a Settings button to the menu that turns it on (Config → General, Graphics → Advanced).
- **A code's page,** as `CheatDetailsFragment`:
  - Name, Creator and Notes (Gecko and graphics mods), and Code.
  - Edit and Delete only for codes the user added.
  - Errors are Android's: "Name can't be empty", "Code can't be empty", "Error on line N", "Lines must either be all encrypted or all decrypted". Encrypted AR codes are decrypted, as `ARCheat.setCheatImpl`.
- **Saving:** each change is saved at once to `GameSettings/<ID>.ini` (`SavePatchSection`, `ActionReplay::SaveCodes`, `Gecko::SaveCodes`), and graphics mods with `GraphicsModGroupConfig::Save`. Android saves when its screen stops; at once is simpler, and nothing reads the file while the page is open.
- **Download Gecko Codes:** `Gecko::DownloadCodes` with the GameTDB ID, on a worker thread. New codes are added unless already there (`CheatsViewModel.addDownloadedGeckoCodes`). The results are Android's:
  - "Downloaded N codes. (added M)";
  - "File contained no codes.";
  - "Failed to download codes." The server answers 404 for games it has no codes for, so that is also what those games get, as on Android.

### Start with Riivolution Patches (`Riivolution.{h,cpp}` as `riivolution`; `RiivolutionPage.qml`)

- **Loading,** as `RiivolutionPatches.loadConfigImpl`: every XML in `Load/Riivolution/riivolution/` that fits the game (`IsValidForGame` with ID, revision and disc number), with the saved choices from `riivolution/config/<ID4>.xml`.
- **The page,** as `RiivolutionBootActivity`:
  - "SD Root: …/Load/Riivolution";
  - for each patch file, its sections and options; tapping an option picks Disabled or one of its choices;
  - **Start** at the bottom.
  - Without patches, it says where the files go: the SD root, and the XML in `…/riivolution`. Android shows an empty list.
- **Saving:** the choices are saved when the page goes and before starting (`saveConfigImpl`, Riivolution's own config format).
- **Starting:** `Emulator::startWithRiivolution` boots as `MainAndroid`'s `Run` with riivolution: `GenerateRiivolutionPatchesFromConfig` and `AddRiivolutionPatches` for a disc. The OpenGL ES restart after a Vulkan failure (stage 4) keeps the patches.

### Convert File (`DiscConverter.{h,cpp}` as `discConverter`; `ConvertPage.qml`)

- **The options,** as `ConvertFragment`, with its lists and defaults (`arrays.xml`):

  | Format | Block size | Compression | Level |
  | --- | --- | --- | --- |
  | ISO | — | — | — |
  | GCZ | 32 KiB | Deflate | — |
  | WIA | 2 MiB | None, Purge, bzip2, LZMA, LZMA2 | 1–9 for bzip2/LZMA/LZMA2 |
  | RVZ | 32 KiB–2 MiB, default 128 KiB | None, bzip2, LZMA, LZMA2, Zstandard (default) | 1–9, Zstandard 1–22; default 5 |

  - The default format is ISO, or RVZ when the game is an ISO already.
  - **Remove Junk Data (Irreversible)** is off by default, and not offered for RVZ or Datel discs.
  - The format explanations are shown below.
- **Warnings,** as Android's, one after another:
  - junk removal into ISO;
  - a Wii GCZ without junk removal;
  - an NKit source.
- **Where to save:** Android asks with the system's save dialog. The port has a folder (the game's folder by default; "Save to" opens the folder picker) and a file name (the game's name with the format's extension). DolphinQt's "The file … already exists. Do you wish to replace it?" comes before replacing a file. Converting a file onto itself is refused.
- **Converting:** `DiscIO::ConvertToPlain`, `ConvertToGCZ` or `ConvertToWIAOrRVZ`, through `ScrubbedBlob` when junk is removed, on a worker thread.
  - A progress panel shows the converter's text ("5570 of 11139 blocks. Compression ratio 87%") and Cancel.
  - The converters delete the output themselves on failure or cancel.
  - Back is disabled while it runs.
  - Results are Android's: "The disc image was successfully converted." (then back to the library, which is refreshed) or "Dolphin failed to complete the requested action.". A cancel shows nothing, as on Android.

### Set as Default ISO

Sets `Core.DefaultISO` (`settingsModel`, global), as `GamePropertiesDialog` does. Config → Paths → Default ISO shows it.

### Test aids

- `main.cpp --start PATH --riivolution` starts with the saved Riivolution choices.
- `page-harness.qml`:
  - `cheats-roundtrip --test-arg ID --test-arg2 GAMETDBID`;
  - `riivolution --test-arg ID`;
  - `convert --test-arg PATH`;
  - `convert-run --test-arg IN --test-arg2 "OUT FORMAT BLOCK COMPRESSION LEVEL SCRUB [CANCEL_MS]"`.

## Checked over adb

Test build, test user folder:

| Check | Result |
| --- | --- |
| Cheats of Newer (SMNW03) | 0 shipped (Dolphin's SMN.ini has only placeholders) |
| Errors | no name → "Name can't be empty"; a bad line → "Error on line 2"; no lines → "Code can't be empty"; plain and encrypted AR lines mixed → "Lines must either be all encrypted or all decrypted" |
| Add, enable, edit, delete | a patch, an AR code and a Gecko code (with creator and two lines of notes) added and enabled; the Gecko code edited; the AR code deleted. `GameSettings/SMNW03.ini`: `[OnFrame]`, `[Gecko]` with `$Test Gecko 2 [Tester]`, and their `_Enabled` sections; no `[ActionReplay]` left |
| Editing a shipped code | refused ("Only codes you added can be edited.", after a fix: at first the line check came first and said "Error on line 1") |
| Gecko download, Mario Kart Wii (RMCE01) | 245 downloaded, 244 added (one already shipped); 3 graphics mods listed |
| Gecko download, Newer and Need for Speed | "Failed to download codes.": the server answers 404 (checked with curl; RMCE01 gets 200) |
| Riivolution, a test XML for SMN | listed ("s64-test.xml", section "Port test", option "Harmless patch"); choice saved to `config/SMNW.xml` (`<option id="Port testHarmless patch" default="1" />`); `--start … --riivolution`: "Riivolution: 1 patches", the game ran. The patch only writes if memory holds a value it does not, so the game was not changed |
| Convert Need for Speed (1.46 GB ISO) to RVZ, Zstandard 5, 128 KiB | 17 s, 1.06 GB (72 %); the RVZ boots and runs at 100 % |
| Cancel (ISO to GCZ, after 4 s) | cancelled, no result dialog, the partial file deleted |
| Pages | the cheats page with the "cheat system is currently disabled" warning and the download result; the Riivolution page with SD root, patch file, option and Start |

The test files were removed afterwards: the RVZ, the XML and its config, and the GameSettings files the tests wrote. `GOWP69.ini` was restored.

## Manual tests

Screen on, unlocked, from an interactive `adb shell`. The test build is deployed; the installed click does not have 6.1–6.4.

`sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 1800`

**S6.4-M1. The game properties.** Long press Newer Super Mario Bros.: are the buttons, in order, Details, Start with Riivolution Patches, Set as Default ISO, Convert File, Perform System Update, Edit Game Settings, Edit Cheats, Clear Game Settings, Start with Vulkan, Start with OpenGL ES, Cancel? Can you scroll to Cancel? On Need for Speed: no Perform System Update?

Exactly the same as described.

**S6.4-M2. Cheats.** Need for Speed → Edit Cheats.
- Is "Dolphin's cheat system is currently disabled." at the top? Tap Settings: Config → General with Enable Cheats; turn it on, Back: is the warning gone? Yes. 
- **Add New AR Code:** name "Test", code `00000000 00000000`, Save. Is it listed? Turn it on, then tap it: Edit and Delete in the header? Delete it. Yes. 
- **Add New Gecko Code** with an empty code: "Code can't be empty"? Exactly.
I also tried a real gecko code and when I entered a game it noticed me that the cheat is loaded but it didn't do anything because the code was for the North American version. Since it was passed to Dolphin I assume the feature works. 
- **Download Gecko Codes:** "Failed to download codes." (the server has none for this game). I saw exactly that. 

**S6.4-M3. Riivolution.** Newer → Start with Riivolution Patches.
- Is "SD Root: …/dolphin-ut-user-w/Load/Riivolution" shown, with the note that there are no patches? Yes. 
- Tap Start: does the game start normally (no patches)? Yes. 
- (Optional: if you have a Riivolution mod for a game you own, put its files in the SD root, with its XML in `…/riivolution`, and try it.) I don't have it so I didn't test it. 

**S6.4-M4. Convert File.** Need for Speed → Convert File.
- Is the format RVZ (the game is an ISO), with Block Size 128 KiB, Compression Zstandard (recommended), Level 5? Is Remove Junk Data greyed out? Yes. 
- Pick GCZ: do the lists change (32 KiB, Deflate, no level) and does the file name end in .gcz? Then back to RVZ. Yes. 
- Convert: does the progress panel count blocks, and after about 20 s does "The disc image was successfully converted." appear? OK: is "Need for Speed - Most Wanted.rvz" in the library, and does it start? Yes.
- Convert again: does it ask whether to replace the file? Say No. 
- Delete the .rvz afterwards if you don't want it (the File Manager, Documents/Dolphin/Games or the test games folder).
These work as expected. 

**S6.4-M5. Set as Default ISO.** Newer → Set as Default ISO. Library ⋮ → Settings → Config → Paths: is Default ISO the Newer file? Press and hold it to reset. Yes. Everything works as expected. 

## Results

All five on-screen tests passed (2026-11-16, test build, test user folder).

| Test | Result |
| --- | --- |
| M1 Game properties | Pass: the buttons in Android's order, scrolling to Cancel; no Perform System Update on Need for Speed |
| M2 Cheats | Pass. The "cheat system is currently disabled" warning, Settings → Enable Cheats, and the warning gone on return. An AR code added, listed, enabled, with Edit and Delete, then deleted. The empty Gecko code gave "Code can't be empty". Download: "Failed to download codes." (none on the server for this game) |
| M2, beyond the test | The user entered a real Gecko code. At the game's start Dolphin's on-screen message said it was loaded; it had no visible effect because the code was for the North American version and Need for Speed here is PAL (GOWP69). So the port hands the codes to Dolphin as intended; whether a code works is down to the code. The log shows the code listed (22:10, "1 Gecko") and later removed (22:13) |
| M3 Riivolution | Pass: SD root and the "no patches" note shown; Start started the game normally. A real Riivolution mod was not tried (none at hand); the patch path was checked over adb with a test XML (above) |
| M4 Convert File | Pass: RVZ, 128 KiB, Zstandard, level 5 by default, Remove Junk Data greyed out; GCZ changed the lists and the extension; the conversion counted blocks and succeeded; the RVZ appeared in the library and started; converting again asked before replacing. The RVZ was deleted afterwards |
| M5 Set as Default ISO | Pass: Config → Paths → Default ISO showed the Newer file; press and hold reset it |

Not covered on screen, for want of files: a working Gecko code for a game at hand, and a real Riivolution mod. Both paths are Dolphin's own code, fed exactly as the Android app feeds it, and were exercised over adb.

# Stage 3.5: controllers and mapping

Goal (`stage3-roadmap.md`): plug in or pair a gamepad and play, without a desktop mapping window.

Status (2026-11-15): on-screen tests passed. One fix (the controller type and source lists on ports 2–4 were empty) waits for a re-test (end of this document).

## What was built

| Part | Where | Notes |
| --- | --- | --- |
| Controllers page | Library ⋮ → **Controllers** (`qml/ControllersPage.qml`) | GameCube Controllers 1–4 with their type, Wii Remotes 1–4 with their source and extension, and the input devices Dolphin sees |
| GameCube port | `qml/GameCubePortPage.qml` | The Android app's types (`gcpadTypeEntries`): None, Standard Controller, Keyboard Controller, Steering Wheel, Dance Mat, DK Bongos, Triforce Baseboard, GameCube Adapter. "Configure" for the emulated pads |
| Wii Remote | `qml/WiimotePage.qml` | Source (None, Emulated, Real Wii Remote) and extension (None, Nunchuk, Classic, Guitar, Drums, Turntable), then the mapping pages the Android app splits it into: **Buttons and D-Pad**, the **extension**, **Motion Simulation** (pointer, swing, tilt, shake), **Motion Input** (accelerometer, gyroscope), **Options, Rumble** |
| Mapping page | `qml/MappingPage.qml`, `Controllers::mapping` → `MappingController` | As the Android app's controller settings (`SettingsFragmentPresenter.addControllerMetaSettings` and `addContainerMappingSettings`): Device, "Map all devices", **Reset to Default**, **Clear**, **Profiles** (header), then each control group: its controls (subtitle: the expression or "Not mapped"), its enable switch, and its settings (sliders, switches) |
| Mapping a control | | **Tap**: "Press a button or move a stick…": Dolphin's `InputDetector` with the Android app's timings (3 s for a first input, 5 s at most), then `MappingCommon::BuildExpression`, as `MotionAlertDialog`. **Press and hold**: the expression as text, as `AdvancedMappingDialog`. Outputs (rumble) cannot be detected, so a tap lists the device's outputs and the phone's vibrator |
| Profiles | | Save, load, delete in Dolphin's profile folders (`Config/Profiles/GCPad/`, `…/Wiimote/`), the same files DolphinQt and the Android app use |
| Gamepad defaults | `GamepadDefaults` in `Controllers.cpp` | Desktop Dolphin's defaults are keyboard keys. For an SDL gamepad, "Reset to Default" and the automatic mapping use a gamepad layout made of SDL3's standard names (positions, so it is the same on Xbox, PlayStation and Nintendo-style pads; table below) |
| Automatic mapping | `Controllers::AutoMap` | When a gamepad (an SDL device) appears, GameCube Controller 1 and Wii Remote 1 (with its Nunchuk and Classic Controller) are mapped to it with the defaults, **if they have no usable device**: none connected, or the touchscreen. A port already on a connected gamepad or keyboard is left alone |
| Rumble on the phone | `PhoneDevice` (`UbuntuTouch/0/Phone`, output "Motor") | A small input device of the frontend's own, like the Android app's "Device Sensors: Motor": a game's rumble mapped to it vibrates the phone through hfd-service (short vibrations back to back while it rumbles). Dolphin drops it when devices are refreshed (it does not come from an input backend), so it is added again |

All changes are made under Dolphin's controller state lock and saved at once (`GCPadNew.ini`, `WiimoteNew.ini`), so they also apply to a running game.

### The gamepad defaults

SDL3 names gamepad controls by position (`Button S/E/W/N` = bottom/right/left/top face button, `Shoulder`, `Trigger`, `Pad N/S/W/E`, `Left/Right X±/Y±`, `Back`, `Start`, `Guide`, `Thumb L/R`).

| Controller | Mapping |
| --- | --- |
| GameCube | A bottom, B left, X right, Y top (the controller's own layout); Z right shoulder; L/R the triggers, digital and analog; Start; D-pad; main stick left stick; C-stick right stick; rumble both motors |
| Wii Remote | A bottom, B right trigger (where it is on the remote), 1 left, 2 right; − Back, + Start, Home Guide; D-pad; pointer on the right stick; shake top button; rumble both motors |
| Nunchuk | Stick left stick; C left shoulder; Z left trigger; shake left stick press |
| Classic Controller | Nintendo's layout (a right, b bottom, x top, y left); L/R the triggers (digital and analog); ZL/ZR the shoulders; −, +, Home; D-pad; both sticks |

These are the port's own choices (Dolphin has no gamepad defaults outside Android, and the Android app maps to its touchscreen). Everything can be remapped.

## Findings

- **Devices on the phone:**
  - `Wayland/0/Keyboard Mouse`, `evdev/0/fts_ts` (the touchscreen), `evdev/0/uinput-goodix` (the fingerprint sensor's keys), and the frontend's `UbuntuTouch/0/Phone`.
  - With the BETOP T6 paired, two more: **`evdev/0/BETOP T6`** and **`SDL/0/Xbox One Wireless Controller`**. SDL3's controller database recognises the pad in Xbox mode and gives it the standard names.
- **The user's Wii Remote 1 was mapped to the touchscreen** (`evdev/0/fts_ts`, from DolphinQt in stage 1, which picks the first device). On the first start with the gamepad paired, the automatic mapping moved it to `SDL/0/Xbox One Wireless Controller` with the Wii Remote and Nunchuk defaults. GameCube port 1 kept its stage 2 mapping (`evdev/0/BETOP T6`, made in DolphinQt), as that device is connected. The previous files are kept as `Config/*.before-3.5` in the test folder.
- **Input detection without a game:** nothing polls the input devices when no game runs, so the mapping page calls `ControllerInterface::UpdateInput()` itself while it waits (as DolphinQt's mapping window does).
- **Checked over adb with Dolphin's pipe input device:**
  - a FIFO in `Pipes/` becomes `Pipe/0/test`;
  - the harness started detection on GameCube A;
  - `PRESS A` was written into the FIFO;
  - result: `` `Pipe/0/test:Button A` ``, saved in `GCPadNew.ini`.
- **Lomiri's `OptionSelector` resets `selectedIndex` when its model is set**, so a bound index showed the first entry. The pages assign it once complete (and again when the device list changes).
- The Android app has **no "hide the overlay while a gamepad is used" setting** (3.3 step 6). The overlay is hidden with "Show Overlay" (3.3b), as in the Android app.
- Not in 3.5:
  - the GameCube **keyboard controller's** mapping (DolphinQt, for now);
  - **hotkeys** (3.6, with the settings);
  - **real Wii Remotes** (Bluetooth passthrough is not set up on the phone).

### Step 5 (optional): the DualShock 4 clone over USB

SDL3's HIDAPI driver could talk to the clone that the kernel's `hid-sony` rejects (stage 1). It needs the clone plugged in, and USB device access for the app user. Not tried yet. If the user plugs it in, the Controllers page shows whether an `SDL/…` device appears; if not, it stays a stone-ut kernel item.

## Manual tests

Screen on, unlocked, the BETOP T6 paired and awake. From an interactive `adb shell`: `sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 900`.

**S3.5-M1. Controllers page.** Library ⋮ → Controllers: the ports and their summaries; the device list at the bottom includes `SDL/0/Xbox One Wireless Controller` and `evdev/0/BETOP T6`. Turn the pad off and on: does the list follow?

Yes. 

**S3.5-M2. Wii Remote 1 on the gamepad (automatic mapping).** Start Newer SMB and play with the pad (Nunchuk layout: left stick, A bottom face button, C/Z on the left shoulder/trigger, B right trigger, 1/2 left/right face buttons). Do the controls work, and does the overlay still work alongside?

Yes. They all work.

**S3.5-M3. Mapping a control.** Controllers → Wii Remote 1 → Buttons and D-Pad. Tap "2", press another button on the pad: is it mapped (the subtitle changes) and does it work in the game? Press and hold a control: edit the expression, Save. Try "Map all devices" on.

Yes. 

**S3.5-M4. GameCube Controller 1.** Configure: the device is `evdev/0/BETOP T6` (the stage 2 mapping). Pick `SDL/0/Xbox One Wireless Controller` as the device, then ⋮ → Reset to Default: the gamepad layout. Play Need for Speed: triggers for throttle and brake, sticks, buttons.

Yes. They all work.

**S3.5-M5. Profiles.** ⋮ → Profiles: save the current mapping as "test", Clear (everything "Not mapped"), Profiles → load "test": back. Delete it.

Yes, everything works. 

**S3.5-M6. Rumble on the phone.** GameCube Controller 1 → Configure → Rumble → Motor: pick "The phone's vibrator". In Need for Speed, does the phone vibrate on crashes? (Picking the gamepad's motors works the same way for pads that rumble.)

Yes. Rumble works. 

**S3.5-M7 (optional).** Plug in the DualShock 4 clone over USB-C: does a new `SDL/…` device appear on the Controllers page?

No. But the same DualShock 4 clone did connect over bluetooth successfully. Wired connection still failed. (that's in run-ut-3.log).

Send the terminal output and the answers.


phablet@ubuntu-phablet:~$ sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 700
running: ut: -u /home/phablet/dolphin-ut-user-w (for 700s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 700s
21:09:590 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
21:12:019 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
22:01:745 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
28:58:080 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
29:59:604 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
31:20:715 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
31:39:052 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
32:06:592 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
phablet@ubuntu-phablet:~$ sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 300
running: ut: -u /home/phablet/dolphin-ut-user-w (for 300s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 300s
33:00:464 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
36:17:791 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
36:46:821 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
37:14:167 Common/FileUtil.cpp:175 W[COMMON]: Delete: /home/phablet/dolphin-ut-user-w/Backup/SYSCONF does not exist
37:50:888 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
phablet@ubuntu-phablet:~$ cd /home/phablet/dolphin-ut-user-w
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ cp run-ut.log run-ut-2.log
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ cd ~
phablet@ubuntu-phablet:~$ (base) alpha@alpha-asus:~/Doc
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ 
(base) alpha@alpha-asus:~/Documents/GitHub/dolphin-ut$ adb shell
phablet@ubuntu-phablet:~$ cd /home/phablet/dolphin-ut-user-w
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ cp run-ut.log run-ut-3.log
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 100
running: ut: -u /home/phablet/dolphin-ut-user-w (for 100s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 100s
48:54:671 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404
49:05:954 Common/FileSearch.cpp:85 E[COMMON]: DoFileSearch error on /home/phablet/dolphin-ut-user-w/Config/Profiles/GCPad/: No such file or directory
49:05:955 Common/FileSearch.cpp:85 E[COMMON]: DoFileSearch error on /home/phablet/dolphin-ut-user-w/Config/Profiles/GCPad/: No such file or directory
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ 
phablet@ubuntu-phablet:~/dolphin-ut-user-w$ sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 100
running: ut: -u /home/phablet/dolphin-ut-user-w (for 100s, log /home/phablet/dolphin-ut-user-w/run-ut.log)
stopped after 100s
51:57:699 Common/HttpRequest.cpp:296 E[COMMON]: Failed to GET https://art.gametdb.com/wii/cover/JA/SMNW03.png: server replied with code 404


Another thing noticed along the way: 

GameCube and Wii controllers 2 through 4: they can not be configured. The only controller type is None. Is it expected? 

Also note that there are 3 logs. Please collect them. 


## Results of S3.5-M1…M7 (2026-11-15)

| Test | Result |
| --- | --- |
| M1 Controllers page, devices following the pad | Yes |
| M2 Wii Remote 1 on the gamepad (automatic mapping), with the overlay alongside | All work |
| M3 Detect a control, edit an expression, map all devices | Yes |
| M4 GameCube 1 on the SDL device, Reset to Default, Need for Speed | All work (triggers, sticks, buttons) |
| M5 Profiles: save, clear, load, delete | All work |
| M6 Rumble on the phone's vibrator (Need for Speed) | Works |
| M7 DualShock 4 clone over USB | **No** device; over **Bluetooth** it works: `evdev/0/Wireless Controller` and **`SDL/0/PS4 Controller`** |
| Also found | **GameCube controllers 2–4 and Wii Remotes 2–4 could not be set up: the type / source list offered only "None".** Fixed (below) |

Log: a `DoFileSearch error … Profiles/GCPad/: No such file or directory` when the profiles dialog opened before any profile existed. Harmless; the folder is no longer searched until it exists.

### Empty lists on ports 2–4

The controller type (GameCube) and source and extension (Wii Remote) lists use Lomiri's `OptionSelector`, which cannot size its open list from a JavaScript array: no width set (the selectors sat in a plain column) and no list height. Opened, the list was empty, so a port set to "None" could not be changed. Port 1 looked fine only because its type was never changed in the tests; the mapping page's device selector, which has a width, worked. Now each selector has the full width and a list height (`containerHeight`). Checked over adb with the page harness, which opens the selector: GameCube Controller 2 lists all eight types.

### The DualShock 4 clone over USB

Over Bluetooth, SDL3 recognises it as a PS4 controller, so the gamepad defaults apply. Over USB nothing can reach it from the app:
- **libusb (SDL's HIDAPI over USB):** the USB device nodes are `root:1018` (`crw-rw----`, Android's `usb` group), and `phablet` is not in group 1018.
- **hidraw:** made only when a HID driver binds, and the kernel's `hid-sony` rejects this clone (stage 1).

So it stays a stone-ut item: a udev rule giving the logged-in user (or `plugdev`) access to USB gamepads would let SDL3's HIDAPI driver handle it; a `hid-sony` fix would give the evdev device too.

### Re-test (stopped here)

**S3.5-M8.** Controllers → GameCube Controller 2 → Controller type: the list shows all types; pick Standard Controller, Configure, then set it back to None. Wii Remote 2 → Wii Remote: Emulated, the extension list works, back to None.

# Stage 6.6: Wii Remote motion from the phone's sensors

Goal (`stage6-roadmap.md`): the Android app's motion controls, where tilting, shaking and turning the phone move the emulated Wii Remote, if Ubuntu Touch lets an app read the sensors. The roadmap asked for a probe first.

Status (2026-11-16): **done.** Probe done (the sensors are reachable), checked over adb, and on screen (S6.6-M1–M5). Open, for the user: a way to recenter the gyroscope pointer on a phone (below).

## The probe

### What the phone has

- **sensorfwd** (Sailfish's sensor framework, 0.15.1 in UBports' packaging) runs as root. On this phone it uses the Android sensor HAL through its HIDL adaptors (`/etc/sensorfw/sensord.conf.d/30-hidl.conf`). It serves the accelerometer, gyroscope, magnetometer, light, proximity, orientation, rotation and pressure sensors.
- **Qt Sensors** exists only for Qt 5 (`libqt5sensors5-sensorfw`). The port is a Qt 6 app and cannot load Qt 5 libraries, so it has to speak sensorfwd's protocol itself.
- **The IIO devices** are the power chips' ADCs (`pm6125`, `pmk8350`), not motion sensors.
- **Access:** sensorfwd's D-Bus service (system bus, `com.nokia.SensorService`) and its data socket (`/run/sensord.sock`, mode 777) are open to the user session. The click runs with AppArmor's `unconfined` template (stage 5), so it can use both.

### The protocol

From sensorfw's sources (`qt-api/socketreader.*`, `core/sockethandler.cpp`, `datatypes/genericdata.h`), checked against the phone:

1. D-Bus `local.SensorManager.loadPlugin("accelerometersensor")`, then `requestSensor(id, pid)` → a session id. (Newer sensorfw drops the pid argument; 0.15.1 has it.)
2. Connect to `/run/sensord.sock` and write the session id (int32). sensorfwd answers with a tag byte (`\n`). The socket must connect soon after the request, or sensorfwd drops the session ("Considering session lost" in its journal).
3. On `/SensorManager/<id>`, `local.AccelerometerSensor` (or `local.GyroscopeSensor`): `setStandbyOverride(session, true)`, `setInterval(session, ms)`, `start(session)`.
4. **The data arrives as batches:** `[uint32 count][count × 24-byte samples]`, each sample `{quint64 timestamp (µs), float x, y, z}`. Floats in this version; older sensorfw had ints. The phone's samples settle it: as ints the values make no sense, as floats they do.
5. **Units:** the accelerometer is in milli-g (the phone lying flat: z = 1012), the gyroscope in milli-degrees per second. The hybris adaptors scale Android's m/s² and rad/s into these.
6. **Stopping:** `stop(session)` and `releaseSensor(id, session, pid)`.

### Rates measured

`ut/phone-debug` probe scripts, 2–3 s each:

| Asked | Accelerometer | Gyroscope |
| --- | --- | --- |
| 10 ms | 93 Hz | 18.5 Hz |
| 5 ms / 200 Hz | 195–199 Hz | 18–20 Hz |
| both together, 5 ms | 195 Hz | 19.7 Hz |

**The gyroscope stays near 20 Hz** whatever is asked. sensorfw's hybris gyroscope adaptor sets a 50 ms default, and requests do not change the rate here: either the HIDL adaptor or the HAL keeps it. It reports a 2.5–1000 ms range, so this looks like an adaptation issue (stone-ut / sensorfw), not something the app controls.

This is still usable: Dolphin integrates the angular rate over time, so turns come out right, only in 50 ms steps. Fast pointing will feel a little coarse.

## How it works

### `PhoneSensors` (`PhoneSensors.{h,cpp}`)

- **The client:** a sensorfwd client in Qt 6, with QtDBus for the calls and a QLocalSocket for the data, on the UI thread's event loop. Each batch's newest sample is kept, converted to Android's units (m/s², rad/s), and stored in atomics for the emulation threads.
- **Started on demand,** as Android suspends unread sensors: an input's first read starts both sensors (checked every 0.5 s). A sensor is stopped 5 s after the last read, so only a game that uses the mapping (a Wii game with motion on) keeps them running.
- **Unavailable sensors:** if sensorfwd cannot be reached, a log line says motion from the phone is off for that run, and the inputs read 0.
- **Screen rotation:** Android turns the x/y axes with the display rotation (`DolphinSensorEventListener.onSensorChanged`). Lomiri does not report its rotation to the app (QScreen stays "portrait" while a game runs in landscape, as logged in 3.2 and here).
  - So in a landscape game window, the side is told from gravity, as the shell does. With the top edge to the left, the device's x axis points up (+1 g: Android's ROTATION_90). With the top edge to the right, it points down (ROTATION_270).
  - The side only changes when that is clear (more than 0.5 g along x, more than along y). The axes are then rotated exactly as Android rotates them.
  - In a portrait window the axes stay as they are. The Emulator reports landscape on each game window resize.
- **Test aid:** `DOLPHIN_UT_SENSOR_LOG=1` logs the screen-axis values, the window shape, the side and the sample counts once a second while running.

### The phone input device

The port's `UbuntuTouch/0/Phone` device (3.5, the vibrator) gains the Android app's sensor inputs, with its names:
- Accel Right / Left / Forward / Backward / Up / Down;
- Gyro Pitch Up / Down, Roll Right / Left, Yaw Left / Right.

Each is half an axis, as Android's `AndroidSensorAxis`. They are not detectable (they always move, so a tap-to-map would catch them), as on Android. Any control can be mapped to them on the mapping pages by holding the control and typing the expression.

### Motion Controls (Wii Remote page)

- **The choice:** Controllers → Wii Remote N → **Motion Controls**, with the Android app's three choices, whose strings are still in its resources (`motionControlsEntries`):
  - **Use Device Sensors (With Pointer Emulation):** the Wii Remote's Accelerometer and Gyroscope groups (Motion Input) mapped to the phone's sensors, as Dolphin's Android defaults (`Wiimote::LoadDefaults`, `#ifdef ANDROID`). Dolphin's gyroscope pointer ("Point", the IMU cursor) is on, so turning the phone aims.
  - **Without Pointer Emulation:** the same mapping, with the gyroscope pointer off, so your finger aims (the touch pointer, 3.3).
  - **Don't Use Device Sensors:** the phone's mappings removed, and the gyroscope pointer off. Motion mapped to something else (a gamepad's gyroscope) is left alone.
- **The default stays "Don't Use".** Dolphin's IMU cursor defaults to on outside Android. Android turns it off "because it prevents touch controls from being used for pointing" (`IMUCursor.cpp`). Turning motion on for everyone would change how existing setups point, so it is a choice per Wii Remote.
- **The choice is read back from the mapping,** so the Motion Input page shows what it set, and editing there shows up here.

## Checked over adb

Test build, test user folder (Wii Remote 1's mapping restored afterwards):

| Check | Result |
| --- | --- |
| Probe: protocol, layout, units | as above; flat phone: accel (5.9, 29.3, 1012) mG; gyro tens of mdps |
| Probe: rates | as in the table above |
| Motion Controls → With Pointer Emulation | Wii Remote 1's IMUAccelerometer and IMUGyroscope groups mapped to `` `UbuntuTouch/0/Phone:Accel Up` `` … `` `…:Gyro Yaw Right` ``; read back as 0 |
| Newer (Wii) with it | the sensors started 1.4 s after the game ran; 100 accel and 20 gyro samples a second; flat phone: accel (0.06, 0.27, 9.93) m/s², gyro 0; portrait window |
| The game stopped, the app left open | both sensors stopped 5 s later ("stopped after 1042 / 207 samples") |
| The Wii Remote page | the selector with the three choices and a short explanation |

Not checkable over adb: which way the motion goes in a game, held in landscape. That is the manual tests.

## Manual tests

Screen on, unlocked, **rotation lock off**, from an interactive `adb shell`. The sensor log is on, so the log shows what the phone reports:

`DOLPHIN_UT_SENSOR_LOG=1 sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 1800`

**S6.6-M1. Turn it on.** Library ⋮ → Controllers → Wii Remote 1 → **Motion Controls** → "Use Device Sensors (With Pointer Emulation)". Back, and in again: still selected?
Yes.

**S6.6-M2. The pointer in the Wii Menu.** Library ⋮ → Wii System → Load Wii System Menu. Hold the phone in **landscape** like a TV remote pointed at a screen (the top edge away from you is not needed: hold it as you hold the game). 
- Turn the phone left and right, and tilt it up and down: does the hand cursor follow, in the right directions?
- Turn the phone round so the other long edge is on top (rotate 180° in landscape), wait for the picture to turn: do the directions still match?
- If the cursor drifts off or starts far from the centre, say how.

All works. Both landscape orientations are correct, but the defaults are a bit ... strange on a phone. 
Tilting fowards is up, tilting backwards is down. Turning counterclockwise is left, and turning clockwise is right. Also the "center/not moving" position is calibrated to "flat on a desk" which isn't exactly a comfortable phone holding position. The default phone holding position sticks the cursor to a side. 
Is this the supposed default on Android dolphin? If so, please ignore this. Otherwise, please recalibrate it. 

**S6.6-M3. Shake and tilt in a game.** Exit the Wii Menu, start Newer Super Mario Bros. In a level:
- Shake the phone: does Mario spin-jump (as shaking the Wii Remote does)?
- In a level with tilting platforms (or the world map's camera), does tilting the phone tilt them?

Yes. 

**S6.6-M4. Without pointer emulation.** Motion Controls → "Use Device Sensors (Without Pointer Emulation)". In the Wii Menu: does your finger aim again (tap empty screen; double tap = A), while shaking still works in Newer?

Yes. 

**S6.6-M5. Off.** Motion Controls → "Don't Use Device Sensors". Shaking does nothing, and the finger aims.

For each test, the time helps: the log has a "phone sensors: accel … side …" line each second.

Yes. Shaking does nothing after turning it off. 

## Results

All five on-screen tests passed (2026-11-16, test build, test user folder).

| Test | Result |
| --- | --- |
| M1 Turn it on | Pass: the choice stayed selected |
| M2 Pointer in the Wii Menu | Pass, **in both landscape orientations**. Tilting the phone forward moves the cursor up, backward down; turning it counterclockwise left, clockwise right. The log shows the side flipping between +1 and −1 as the phone turned round. Remark: the cursor's centre is "phone flat on a desk", so the usual way of holding a phone keeps the cursor at an edge (below) |
| M3 Shake and tilt in Newer | Pass: shaking spin-jumps, tilting tilts |
| M4 Without pointer emulation | Pass: the finger aims in the Wii Menu, shaking still works in Newer |
| M5 Off | Pass: shaking does nothing, the finger aims |

### The pointer's centre: Dolphin's, as on Android

- **Why flat:** Dolphin's gyroscope pointer (`EmulateIMUCursor`, `Core/HW/WiimoteEmu/Dynamics.cpp`) aims at the screen's centre when the Wii Remote is level, as a real Wii Remote pointed at a TV is. With the Android app's mapping, which the port uses unchanged, the Wii Remote's "forward" is the phone's screen-up axis and its "up" is out of the screen. So "level" is the phone lying flat with its top away from you, and the usual hold (the screen tilted towards you) points the Wii Remote upward.
- **Android is the same:** Android Dolphin has the same mapping, and its pointer behaves the same when turned on. It is off there by default, for the touch pointer's sake.
- **Dolphin's answer is the pointer's "Recenter" input** (Motion Input → Point → Recenter). Pressed, it takes the current hold as the centre: pitch as held (`recentered_pitch`), and yaw back to the middle. It is not mapped by default anywhere.
- **What the port could add (the user's call):**
  - map Recenter for the phone, for example to a button of the touch overlay;
  - or recenter once when a game starts with pointer emulation on.
  - Either is a frontend mapping, as the choice itself is. Until then, a gamepad or keyboard button can be mapped to Recenter on that page.

### The slow Wii Menu exit (once): Dolphin's WiiConnect24, waiting on a dead server

- **The third Wii Menu run** took 59 s to stop: "core: stopping" at 23:18:38.856, idle at 23:19:37.520. The other two stopped in 0.15 s.
- **What it waited on:** the log at 23:19:37.396 shows Dolphin's WiiConnect24 emulation (`IOS/Network/KD/NetKDRequest.cpp`) failing a download from Nintendo's old Mario Kart Wii ghost server (`mariokartwii.sake.gs.nintendowifi.net`), with "Connection timed out". The stop completed 0.1 s after that request gave up. The emulated Wii's background task was downloading when Exit was pressed, and Dolphin's shutdown waited for the request to time out.
- **Not the port:** the port only asks the core to stop (as at the other exits). The wait is Dolphin's network emulation against a server that no longer exists, so it happens only when that task is mid-request, which is why only one exit of three was slow.
- **If it bothers:** Dolphin's setting "Enable WiiConnect24 via WiiLink" (Config → Wii) chooses that service's servers. Whether these requests can be made not to block shutdown is Dolphin's code (the core), not the port's.
 
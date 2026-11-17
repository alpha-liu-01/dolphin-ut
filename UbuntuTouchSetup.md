# How to Build Dolphin for Ubuntu Touch

This fork adds an unofficial port of Dolphin to [Ubuntu Touch](https://ubuntu-touch.io): a
touch-first app (`dolphin-ut`, in `Source/Core/DolphinUT`) modeled on Dolphin's Android app, packaged
as a click. It is not supported by the Dolphin team. This guide covers building it, packaging it,
installing it, and the development loop on a phone.

The port's design notes, test results and decisions are in [docs/ut-port](docs/ut-port/README.md).

## Target

- **Ubuntu Touch 24.04-2.x, arm64** (the click's framework is `ubuntu-touch-24.04-2.x`).
- **Graphics:** the phone's Android GPU driver through libhybris: Vulkan (the default) or OpenGL ES,
  rendering on Lomiri's Wayland.
- **Tested on one phone:** the Redmi Note 12 5G (`sunstone`, Adreno 619). Other devices have other GPU
  drivers and libhybris builds and are untested. Reports are welcome.

## Prerequisites

- An **x86_64 Linux** computer with **Docker** (your user in the `docker` group), **git**, and
  about 10 GB of free disk space (the source with its submodules about 7 GB, the container image
  2.3 GB, the build trees a few hundred MB).
- For installing and testing: a phone running Ubuntu Touch 24.04-2.x with **developer mode** on
  (System Settings → About → Developer Mode) and **adb** on the computer.

Everything is compiled in a cross-build container, so nothing else needs to be installed on the
computer or on the phone.

## 1. Get the source

```sh
git clone https://github.com/alpha-liu-01/dolphin-ut.git
cd dolphin-ut
git submodule update --init --recursive
```

## 2. The cross-build container

`ut/docker/Dockerfile` defines the image `dolphin-ut-cross`:
- Ubuntu 24.04 (amd64) with GCC 13 for arm64 and CMake;
- the phone's own arm64 libraries, through multiarch, from Ubuntu noble and from UBports'
  `24.04-2.x` repository. That repository is pinned above noble, as on the phone, so Qt is the
  phone's Qt 6.10.2 and not noble's 6.4;
- `click`, and the Qt 6 Wayland shell plugin that the phone lacks and the click bundles.

`ut/build.sh` builds the image the first time it runs. To rebuild it, for example after UBports
updates Qt, add `--image`:

```sh
ut/build.sh --image
```

## 3. Build

### The release build (what the click ships)

```sh
ut/build.sh --release
```

- **Output:** `build-ut/release/Binaries/dolphin-ut`.
- **Only the app is built:** DolphinQt, dolphin-emu-nogui and dolphin-tool are left out.
- **No development aids:** `-DDOLPHIN_UT_DEV_TOOLS=OFF` leaves out the test options, the
  environment hooks and the extra logging (see "Development builds" below).
- **Build time:** the first build compiles all of Dolphin. Later builds only rebuild what changed.

### The development build

```sh
ut/build.sh            # build-ut/dolphin
ut/build.sh --package  # also build-ut/dolphin-ut-arm64.tar.gz: binaries, Sys/, Languages/
```

The development build also builds DolphinQt, dolphin-emu-nogui and dolphin-tool, and keeps the
development aids that the test scripts in `ut/phone-debug` use.

Both builds take `--reconfigure` (wipe the CMake cache first). Extra CMake options can be passed in
`DOLPHIN_CMAKE_ARGS`.

## 4. Package the click

```sh
ut/build.sh --release && ut/click/build-click.sh
```

**Output:** `build-ut/<name>_<version>_arm64.click`. It holds:
- `dolphin-ut` (stripped) and Dolphin's `Sys/`;
- the launcher, the desktop file, and the AppArmor and content-hub hooks (`ut/click/`);
- the Qt 6 Wayland shell plugin;
- `lib/hybris/libvndksupport.so`, a small stand-in library that lets the app load custom Vulkan
  drivers such as Turnip. `ut/vkdriver/build-shim.sh` builds it without any files from a phone.

**Settings,** through the environment:

| Variable | Default | Meaning |
| --- | --- | --- |
| `CLICK_NAME` | `dolphin-ut.alphaliu01` | The package name. It also names the app's folders on the phone (`~/.local/share/<name>`), so a new name starts with empty user data |
| `CLICK_MAINTAINER` | git's `user.name <user.email>` | The maintainer field. It is read at build time and not stored in the repository |
| `CLICK_VERSION` | Dolphin's version (`2609-276` → `2609.276`) | The click version. Reinstalling the same version keeps the old AppArmor profile, so a changed `.apparmor` file needs a new version |
| `CLICK_CONFINED=1` | off | Build under the confined template instead of `unconfined`. Gamepads do not work confined (`/dev/input` is denied), so the release is unconfined |

**Source link (GPL):** the click's description names the source repository and commit it was built
from. A build with uncommitted changes says so. Publish releases from a pushed commit, so that the
source can be found.

## 5. Install on the phone

With the phone connected over adb:

```sh
ut/click/install-click.sh                     # the newest click in build-ut/
ut/click/install-click.sh path/to/file.click  # or a given one
```

This copies the click to the phone and installs it through the click service. It is what clickable
does, since 24.04 has no `pkcon`. You can also copy the `.click` file to the phone and open it with
the File Manager, which hands it to OpenStore.

The app then appears in the app drawer as **Dolphin**. Games go in `Documents/Dolphin/Games`,
which is visible over USB, or come in through **Import Games** in the library.

## 6. Development on the phone

The scripts in `ut/phone-debug` run the development build from adb, with the environment Lomiri
gives an app. Set up once:

```sh
# The scripts, on the phone in /home/phablet/dolphin-ut-probe
adb shell mkdir -p /home/phablet/dolphin-ut-probe
adb push ut/phone-debug/. /home/phablet/dolphin-ut-probe/

# The Qt 6 Wayland shell plugin (the phone has none), from the container
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src dolphin-ut-cross \
  sh -c 'rm -rf build-ut/qt6w && cp -r /opt/ut-bundle build-ut/qt6w'
adb shell mkdir -p /home/phablet/dolphin-ut-probe/qt6w
adb push build-ut/qt6w/. /home/phablet/dolphin-ut-probe/qt6w/
```

Then, after each build:

```sh
ut/build.sh --package
adb push build-ut/dolphin-ut-arm64.tar.gz /home/phablet/
adb shell 'tar -xzf dolphin-ut-arm64.tar.gz -C /home/phablet'   # /home/phablet/dolphin-ut-arm64
# For custom GPU drivers, the stand-in library too:
ut/vkdriver/build-shim.sh
adb shell mkdir -p /home/phablet/dolphin-ut-arm64/lib/hybris
adb push build-ut/vkdriver/libvndksupport.so /home/phablet/dolphin-ut-arm64/lib/hybris/
```

To copy only a rebuilt app, push `build-ut/dolphin/Binaries/dolphin-ut` over
`/home/phablet/dolphin-ut-arm64/dolphin-ut`.

**Running it** (with the phone's screen on and unlocked for anything you want to see):

```sh
adb shell
sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut 600         # the app, for 600 s
GAME=/path/to/game.iso sh /home/phablet/dolphin-ut-probe/run-dolphin.sh ut-start 120
```

- **The log** is in `<user folder>/run-<mode>.log`. The user folder is
  `/home/phablet/dolphin-ut-user-w` by default; `DOLPHIN_UT_USER=…` chooses another. A crash keeps
  its core dump and memory map in `crash-<date>-<mode>/` there; `core-info.py` and `core-stack.py`
  read them on the phone.
- **From the app drawer:** `sh /home/phablet/dolphin-ut-probe/dev-install.sh` adds a "Dolphin (dev)"
  entry that runs this build like an installed app, with an app ID and as a content-hub
  destination; `dev-install.sh remove` takes it away.
- **Development aids** (development builds only):
  - `--start PATH` boots a game; `--state-slot N` loads a save state once it runs;
  - `--qml FILE` with `page-harness.qml` opens a single page for a check, and `--grab PNG` saves a
    screenshot of the app's window;
  - `--test-alert`, `--answer-after`, `--test-action`, `--test-restart-after` exercise alerts,
    actions and the restart;
  - `DOLPHIN_UT_OVERLAY_MENU=…` opens a page of the in-game menu at the start;
    `DOLPHIN_UT_SENSOR_LOG=1` logs the motion sensors;
  - the App Log page has the development switches.

  The comments at the top of each script and in `main.cpp` describe them.
- **Screen captures over adb** (screen unlocked): source `ut-env.sh`, then
  `mirscreencast -m /run/user/32011/mir_socket -n 1 -f out.raw` gives one 1080×2400 RGBA frame.

## Generated files

Two files are generated from the Android app's sources, so that its menus and overlay stay the
single source:

| File | Generator | Run it after |
| --- | --- | --- |
| `Source/Core/DolphinUT/settings.json`, `SettingsTable.inc` | `ut/tools/gen-settings.py` | changes to the Android app's settings (`features/settings`, `strings.xml`, `arrays.xml`) |
| `Source/Core/DolphinUT/OverlayDefaults.inc` | `ut/tools/gen-overlay-defaults.py > Source/Core/DolphinUT/OverlayDefaults.inc` | changes to the Android app's overlay defaults (`InputOverlay.kt`, `integers.xml`) |

Both need only Python 3 on the computer. Commit the generated files with the change that needs them.

## Where the port's code is

| Path | What |
| --- | --- |
| `Source/Core/DolphinUT/` | The app: C++ (Qt 6) and its QML pages (`qml/`) |
| `Source/Core/DolphinUT/CMakeLists.txt` | The `dolphin-ut` target; `DOLPHIN_UT_DEV_TOOLS` |
| `ut/docker/` | The cross-build container and its CMake toolchain |
| `ut/build.sh` | Builds (development or `--release`) in the container |
| `ut/click/` | Click packaging, install script, launcher, hooks |
| `ut/vkdriver/` | The stand-in `libvndksupport.so` for custom GPU drivers |
| `ut/tools/` | The generators above |
| `ut/phone-debug/` | Scripts for running and debugging on the phone over adb |
| `docs/ut-port/` | Design notes, test results and decisions, stage by stage |

The rest of the tree is Dolphin's. The port also carries upstream pull request #14962 (Wayland
support), which Dolphin's own Linux build uses too.

## Troubleshooting

- **`docker: permission denied`:** add your user to the `docker` group and log in again.
- **The container cannot find Qt 6.10 packages:** UBports updated its repository. Rebuild the image
  (`ut/build.sh --image`). The build needs the same Qt as the phone, so update the phone too if its
  Qt changed.
- **On the phone, "cannot allocate memory in static TLS block":** a program was started without
  libhybris' TLS settings. Use the scripts, or source `/home/phablet/dolphin-ut-probe/ut-env.sh` in
  the adb shell first.
- **"no release build: run ut/build.sh --release first"** from `build-click.sh`: the click packages
  the release build, not the development one.
- **The app does not open from the drawer after `install-click.sh`:** look at its output in the
  session journal (`sh /home/phablet/dolphin-ut-probe/click-log.sh` on the phone).

## Contributing

Read [Contributing.md](Contributing.md). Changes that alter how the emulated consoles behave belong
in upstream Dolphin, not in this port. AI agents must follow [AGENTS.md](AGENTS.md).

#!/bin/sh
# Cross-build Dolphin for Ubuntu Touch (arm64) in the dolphin-ut-cross container.
#
#   ut/build.sh                 configure (first time) and build into build-ut/dolphin
#   ut/build.sh --reconfigure   wipe the CMake cache first
#   ut/build.sh --package       also make build-ut/dolphin-ut-arm64.tar.gz (binaries + Sys)
#   ut/build.sh --release       the release build for the click instead (stage 7): build-ut/release,
#                               dolphin-ut only, without its development aids
#                               (-DDOLPHIN_UT_DEV_TOOLS=OFF); ut/click/build-click.sh packages it
#   ut/build.sh --image         (re)build the container image first
#
# Extra CMake options can be passed in $DOLPHIN_CMAKE_ARGS.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
image=dolphin-ut-cross
build=build-ut/dolphin
reconfigure=0
package=0
release=0
target=

for arg in "$@"; do
  case "$arg" in
    --reconfigure) reconfigure=1 ;;
    --package) package=1 ;;
    --release) release=1 ;;
    --image) docker build -t "$image" "$root/ut/docker" ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

# The release build: only the app, without its development aids (the frontends and tools the click
# does not ship are left out, so this tree builds less).
release_args=
if [ "$release" = 1 ]; then
  build=build-ut/release
  target=dolphin-ut
  release_args="-DDOLPHIN_UT_DEV_TOOLS=OFF -DENABLE_QT=OFF -DENABLE_NOGUI=OFF -DENABLE_CLI_TOOL=OFF"
fi

docker image inspect "$image" >/dev/null 2>&1 || docker build -t "$image" "$root/ut/docker"

[ "$reconfigure" = 1 ] && rm -rf "$root/$build/CMakeCache.txt" "$root/$build/CMakeFiles"

# The bundled curl (USE_SYSTEM_LIBS=OFF) only looks for a CA store when not cross-compiling, so
# without CURL_CA_BUNDLE/CURL_CA_PATH every HTTPS request (GameTDB covers, ...) fails to verify.
# Ubuntu Touch keeps them in the usual Debian places (docs/ut-port/stage3.1-library.md).
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$root:/src" -w /src "$image" sh -euc "
  if [ ! -f $build/CMakeCache.txt ]; then
    cmake -S . -B $build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=/opt/aarch64-linux-gnu.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DUSE_SYSTEM_LIBS=OFF \
      -DENABLE_QT=ON -DENABLE_NOGUI=ON -DENABLE_CLI_TOOL=ON \
      -DENABLE_TESTS=OFF -DENABLE_LLVM=OFF \
      -DENABLE_AUTOUPDATE=OFF -DENABLE_ANALYTICS=OFF -DUSE_DISCORD_PRESENCE=OFF \
      -DENCODE_FRAMEDUMPS=OFF \
      -DENABLE_UT_FRONTEND=ON \
      -DCURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt -DCURL_CA_PATH=/etc/ssl/certs \
      $release_args ${DOLPHIN_CMAKE_ARGS:-}
  fi
  ninja -C $build $target
"

if [ "$package" = 1 ]; then
  out="$root/build-ut/dolphin-ut-arm64"
  rm -rf "$out"
  mkdir -p "$out"
  for b in dolphin-ut dolphin-emu dolphin-emu-nogui dolphin-tool; do
    [ -f "$root/$build/Binaries/$b" ] && cp "$root/$build/Binaries/$b" "$out/"
  done
  cp -r "$root/Data/Sys" "$out/Sys"
  [ -d "$root/$build/Binaries/Languages" ] && cp -r "$root/$build/Binaries/Languages" "$out/Languages"
  tar -C "$root/build-ut" -czf "$root/build-ut/dolphin-ut-arm64.tar.gz" dolphin-ut-arm64
  ls -l "$root/build-ut/dolphin-ut-arm64.tar.gz"
fi

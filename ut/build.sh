#!/bin/sh
# Cross-build Dolphin for Ubuntu Touch (arm64) in the dolphin-ut-cross container.
#
#   ut/build.sh                 configure (first time) and build into build-ut/dolphin
#   ut/build.sh --reconfigure   wipe the CMake cache first
#   ut/build.sh --package       also make build-ut/dolphin-ut-arm64.tar.gz (binaries + Sys)
#   ut/build.sh --image         (re)build the container image first
#
# Extra CMake options can be passed in $DOLPHIN_CMAKE_ARGS.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
image=dolphin-ut-cross
build=build-ut/dolphin
reconfigure=0
package=0

for arg in "$@"; do
  case "$arg" in
    --reconfigure) reconfigure=1 ;;
    --package) package=1 ;;
    --image) docker build -t "$image" "$root/ut/docker" ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

docker image inspect "$image" >/dev/null 2>&1 || docker build -t "$image" "$root/ut/docker"

[ "$reconfigure" = 1 ] && rm -rf "$root/$build/CMakeCache.txt" "$root/$build/CMakeFiles"

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
      ${DOLPHIN_CMAKE_ARGS:-}
  fi
  ninja -C $build
"

if [ "$package" = 1 ]; then
  out="$root/build-ut/dolphin-ut-arm64"
  rm -rf "$out"
  mkdir -p "$out"
  for b in dolphin-emu dolphin-emu-nogui dolphin-tool; do
    [ -f "$root/$build/Binaries/$b" ] && cp "$root/$build/Binaries/$b" "$out/"
  done
  cp -r "$root/Data/Sys" "$out/Sys"
  [ -d "$root/$build/Binaries/Languages" ] && cp -r "$root/$build/Binaries/Languages" "$out/Languages"
  tar -C "$root/build-ut" -czf "$root/build-ut/dolphin-ut-arm64.tar.gz" dolphin-ut-arm64
  ls -l "$root/build-ut/dolphin-ut-arm64.tar.gz"
fi

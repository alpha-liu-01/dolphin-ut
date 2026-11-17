#!/bin/sh
# Builds the click package (stage 5, docs/ut-port/stage5-packaging.md) from the cross build:
#
#   ut/build.sh --release && ut/click/build-click.sh      build-ut/<name>_<version>_arm64.click
#
# It packages the release build (build-ut/release: no development aids, stage 7), not the
# development build in build-ut/dolphin.
#
# Install on the phone (24.04 has no pkcon; this is what clickable does): ut/click/install-click.sh,
# or adb push the .click to /home/phablet/ and run
#   gdbus call --system --dest com.lomiri.click --object-path /com/lomiri/click \
#     --method com.lomiri.click.Install /home/phablet/<file>.click
#
# Environment:
#   CLICK_NAME        the package name (default dolphin-ut.alphaliu01). It names the app's folders
#                     (~/.local/share/<name>, ...), so changing it starts with empty user data.
#   CLICK_MAINTAINER  "Name <email>" (default: git config user.name and user.email of this checkout;
#                     not stored in the repository).
#   CLICK_CONFINED=1  drop "template": "unconfined" from dolphin-ut.apparmor, for testing under the
#                     confined ubuntu-sdk template with the listed policy groups. The click ships
#                     unconfined (user decision, stage 5): the confined template denies /dev/input
#                     and udev's device data, so no gamepads (stage5-packaging.md).
#   CLICK_VERSION     the click version (default: Dolphin's, 2609-265 → 2609.265). Reinstalling the
#                     same version keeps the old AppArmor profile, so a changed .apparmor file needs
#                     a new version (for example 2609.265.1).
#
# The click holds dolphin-ut (stripped), Dolphin's Sys/ next to it (where Dolphin looks first), the
# launcher, the hooks, the Qt 6 Wayland shell integration from the cross container
# (/opt/ut-bundle, ut/docker/Dockerfile), and the stand-in libvndksupport.so for custom GPU drivers
# in lib/hybris/ (stage 4, ut/vkdriver). Everything else is on the phone (stage 1).
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
build=build-ut/release
name=${CLICK_NAME:-dolphin-ut.alphaliu01}
confined=${CLICK_CONFINED:-0}
scmrev="$root/$build/Source/Core/Common/scmrev.h"
[ -f "$root/$build/Binaries/dolphin-ut" ] && [ -f "$scmrev" ] || {
  echo "no release build: run ut/build.sh --release first" >&2
  exit 1
}
maintainer=${CLICK_MAINTAINER:-"$(git -C "$root" config user.name) <$(git -C "$root" config user.email)>"}
# Dolphin's version, "2609-264" (or "2609-264-dirty"), as the click version 2609.264.
desc=$(sed -n 's/^#define SCM_DESC_STR "\(.*\)"$/\1/p' "$scmrev")
commit=$(sed -n 's/^#define SCM_REV_STR "\(.*\)"$/\1/p' "$scmrev")
version=${CLICK_VERSION:-$(echo "$desc" | sed 's/-dirty$//; s/-/./g')}
# GPL: where this exact source is. A build with uncommitted changes says so.
remote=$(git -C "$root" remote get-url origin 2>/dev/null | sed 's|^git@github.com:|https://github.com/|; s|\.git$||')
source="${remote:-this fork} at commit $commit"
case "$desc" in *-dirty) source="$source, with uncommitted changes (a development build)" ;; esac

# The GPU driver shim (7.7 KB, built from ut/vkdriver without files from a phone).
sh "$root/ut/vkdriver/build-shim.sh" >/dev/null

stage=build-ut/click/$name
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$root:/src" -w /src dolphin-ut-cross sh -euc "
  rm -rf '$stage'
  mkdir -p '$stage/lib/aarch64-linux-gnu/qt6/plugins/wayland-shell-integration' '$stage/lib/hybris'
  cp build-ut/vkdriver/libvndksupport.so '$stage/lib/hybris/'
  aarch64-linux-gnu-strip -o '$stage/dolphin-ut' '$build/Binaries/dolphin-ut'
  cp -r Data/Sys '$stage/Sys'
  cp Data/dolphin-emu.svg '$stage/dolphin-ut.svg'
  cp ut/click/launcher.sh ut/click/dolphin-ut.desktop ut/click/dolphin-ut.apparmor \
     ut/click/dolphin-ut-content.json '$stage/'
  chmod 755 '$stage/launcher.sh' '$stage/dolphin-ut'
  if [ '$confined' = 1 ]; then
    sed -i '/\"template\": \"unconfined\"/d' '$stage/dolphin-ut.apparmor'
  fi
  q=/opt/ut-bundle/usr/lib/aarch64-linux-gnu
  cp -L \$q/libQt6WlShellIntegration.so.6 '$stage/lib/aarch64-linux-gnu/'
  cp \$q/qt6/plugins/wayland-shell-integration/libxdg-shell.so \
     '$stage/lib/aarch64-linux-gnu/qt6/plugins/wayland-shell-integration/'
  sed -e 's|@NAME@|$name|' -e 's|@VERSION@|$version|' -e 's|@MAINTAINER@|$maintainer|' \
      -e 's|@SOURCE@|$source|' ut/click/manifest.json.in > '$stage/manifest.json'
  cd build-ut && click build --no-validate 'click/$name'
"
ls -l "$root/build-ut/${name}_${version}_arm64.click"

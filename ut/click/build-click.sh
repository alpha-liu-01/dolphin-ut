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
#   CLICK_VERSION     the click version before its release suffix (default: Dolphin's, 2609-265 →
#                     2609.265). Reinstalling the same version keeps the old AppArmor profile, so a
#                     changed .apparmor file needs a new version (for example 2609.265.1).
#   CLICK_UT          the Ubuntu Touch release: 24.04-2.x (default) or 24.04-1.x. 24.04-1.x has no
#                     Qt 6, so its click also bundles the Qt 6 runtime that the cross container
#                     unpacks into /opt/ut-runtime-1x (ut/docker/runtime-24.04-1.x.list): Qt 6.10.2,
#                     Lomiri Components and Content for Qt 6, maliit's Qt 6 input context, and
#                     libhybris' Vulkan with its Wayland plugin, which 1.x lacks. It goes to
#                     build-ut/ut-24.04-1.x/ (stage 7, docs/ut-port/stage7-release.md).
#
# The version ends in the release: ".2x" for 24.04-2.x, ".1x" for 24.04-1.x (2609.278.2x and
# 2609.278.1x, also after a CLICK_VERSION). OpenStore needs a unique version per click and offers a
# phone the highest one it can install; 2.x phones accept the 1.x framework too, and 2609.278.1x
# sorts below 2609.278.2x, so they get the 2.x click. Not "~1x", which would sort below a bare
# 2609.278: systemd escapes "~" in the app's unit name (\x7e), and Lomiri then cannot read the app
# ID back ("Invalid app ID"): the app ran unmanaged, not paused in the background (stage 7).
#
# The click holds dolphin-ut (stripped), Dolphin's Sys/ next to it (where Dolphin looks first), the
# launcher, the hooks, the Qt 6 Wayland shell integration from the cross container
# (/opt/ut-bundle, ut/docker/Dockerfile), and the stand-in libvndksupport.so for custom GPU drivers
# in lib/hybris/ (stage 4, ut/vkdriver). Everything else is on the phone (stage 1), except on
# 24.04-1.x (CLICK_UT above).
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
build=build-ut/release
name=${CLICK_NAME:-dolphin-ut.alphaliu01}
confined=${CLICK_CONFINED:-0}
ut=${CLICK_UT:-24.04-2.x}
case "$ut" in
  24.04-2.x) out=build-ut; policy=2404.2; bundle=0; suffix=.2x ;;
  24.04-1.x) out=build-ut/ut-24.04-1.x; policy=2404.1; bundle=1; suffix=.1x ;;
  *) echo "CLICK_UT: 24.04-2.x or 24.04-1.x, not $ut" >&2; exit 2 ;;
esac
scmrev="$root/$build/Source/Core/Common/scmrev.h"
[ -f "$root/$build/Binaries/dolphin-ut" ] && [ -f "$scmrev" ] || {
  echo "no release build: run ut/build.sh --release first" >&2
  exit 1
}
maintainer=${CLICK_MAINTAINER:-"$(git -C "$root" config user.name) <$(git -C "$root" config user.email)>"}
# Dolphin's version, "2609-264" (or "2609-264-dirty"), as 2609.264, then the release suffix.
desc=$(sed -n 's/^#define SCM_DESC_STR "\(.*\)"$/\1/p' "$scmrev")
commit=$(sed -n 's/^#define SCM_REV_STR "\(.*\)"$/\1/p' "$scmrev")
version=${CLICK_VERSION:-$(echo "$desc" | sed 's/-dirty$//; s/-/./g')}$suffix
# GPL: where this exact source is. A build with uncommitted changes says so.
remote=$(git -C "$root" remote get-url origin 2>/dev/null | sed 's|^git@github.com:|https://github.com/|; s|\.git$||')
source="${remote:-this fork} at commit $commit"
case "$desc" in *-dirty) source="$source, with uncommitted changes (a development build)" ;; esac

# The GPU driver shim (7.7 KB, built from ut/vkdriver without files from a phone).
sh "$root/ut/vkdriver/build-shim.sh" >/dev/null

stage=build-ut/click-$ut/$name
mkdir -p "$root/$out"
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
  sed -i 's/\"policy_version\": .*/\"policy_version\": $policy/' '$stage/dolphin-ut.apparmor'
  q=/opt/ut-bundle/usr/lib/aarch64-linux-gnu
  cp -L \$q/libQt6WlShellIntegration.so.6 '$stage/lib/aarch64-linux-gnu/'
  cp \$q/qt6/plugins/wayland-shell-integration/libxdg-shell.so \
     '$stage/lib/aarch64-linux-gnu/qt6/plugins/wayland-shell-integration/'
  if [ '$bundle' = 1 ]; then
    # 24.04-1.x: the Qt 6 runtime. The libraries without the EGLFS, xcb and compositor ones, the
    # plugins the app uses, all QML modules; the launcher points Qt and libhybris at them.
    r=/opt/ut-runtime-1x/usr/lib/aarch64-linux-gnu
    l='$stage/lib/aarch64-linux-gnu'
    for f in \$r/lib*.so*; do
      case \$f in *XcbQpa*|*EglFS*|*EglFs*|*WaylandEglCompositor*|*WlShellIntegration*) continue ;; esac
      cp -P \$f \$l/
    done
    for p in platforms/libqwayland.so wayland-graphics-integration-client/libqt-plugin-wayland-egl.so \
             platforminputcontexts/libmaliitplatforminputcontextplugin.so \
             imageformats iconengines tls; do
      mkdir -p \$l/qt6/plugins/\$(dirname \$p)
      cp -r \$r/qt6/plugins/\$p \$l/qt6/plugins/\$p
    done
    cp -r \$r/qt6/qml \$l/qt6/qml
    # libhybris' Vulkan Wayland plugin (HYBRIS_VULKANPLATFORM_DIR in the launcher) and its library.
    h=/opt/ut-hybris-1x/usr/lib/aarch64-linux-gnu
    mkdir -p \$l/libhybris
    cp \$h/libhybris/vulkanplatform_wayland.so \$l/libhybris/
    cp -P \$h/libhybris-vulkanplatformcommon.so.1* \$l/
    # The licenses of what is bundled.
    mkdir -p '$stage/licenses'
    for d in /opt/ut-runtime-1x/usr/share/doc/* /opt/ut-hybris-1x/usr/share/doc/libhybris; do
      if [ -f \$d/copyright ]; then
        mkdir -p '$stage/licenses/'\$(basename \$d)
        cp \$d/copyright '$stage/licenses/'\$(basename \$d)/
      fi
    done
  fi
  sed -e 's|@NAME@|$name|' -e 's|@VERSION@|$version|' -e 's|@MAINTAINER@|$maintainer|' \
      -e 's|@SOURCE@|$source|' -e 's|@FRAMEWORK@|ubuntu-touch-$ut|' \
      ut/click/manifest.json.in > '$stage/manifest.json'
  cd '$out' && click build --no-validate '/src/$stage'
"
ls -l "$root/$out/${name}_${version}_arm64.click"

# Cross-build environment for Dolphin on Ubuntu Touch 24.04-2.x (arm64).
#
# Host: amd64 Ubuntu 24.04. Target: arm64, from the same archives the phone uses:
# Ubuntu noble (ports.ubuntu.com) plus repo.ubports.com 24.04-2.x pinned at 2000,
# so Qt 6 is the phone's 6.10.2, not noble's 6.4.
#
# Build:  docker build -t dolphin-ut-cross ut/docker
# Use:    ut/build.sh (mounts the source tree at /src)

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8

# amd64 from archive.ubuntu.com, arm64 from ports.ubuntu.com.
RUN sed -i 's/^Types: deb$/Types: deb\nArchitectures: amd64/' /etc/apt/sources.list.d/ubuntu.sources \
 && printf '%s\n' \
      'Types: deb' 'Architectures: arm64' 'URIs: http://ports.ubuntu.com/ubuntu-ports/' \
      'Suites: noble noble-updates noble-security' 'Components: main universe restricted multiverse' \
      'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg' \
      > /etc/apt/sources.list.d/ubuntu-ports.sources \
 && dpkg --add-architecture arm64

# UBports 24.04-2.x, as on the phone (/etc/apt/sources.list.d/ubports.list, preferences.d/ubports.pref).
COPY ubports.gpg /etc/apt/trusted.gpg.d/ubports.gpg
RUN echo 'deb [arch=amd64,arm64] http://repo.ubports.com/ 24.04-2.x main' > /etc/apt/sources.list.d/ubports.list \
 && printf '%s\n' 'Package: *' 'Pin: release o=UBports,a=24.04-2.x' 'Pin-Priority: 2000' \
      > /etc/apt/preferences.d/ubports.pref

# Host tools. qt6-base-dev(-tools) for amd64 provide moc/rcc/uic and the Qt6*Tools CMake packages
# that a cross build finds through QT_HOST_PATH; they must be the same Qt version as the target.
# qemu-user-static satisfies libglib2.0-dev:arm64's "python3 | qemu-user | qemu-user-static"
# (otherwise apt picks python3:arm64, whose postinst cannot run here) and can run arm64 test binaries.
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates git cmake ninja-build pkgconf python3 file qemu-user-static \
      crossbuild-essential-arm64 \
      gettext libwayland-bin wayland-protocols \
      qt6-base-dev qt6-base-dev-tools \
      qt6-declarative-dev qt6-declarative-dev-tools \
 && rm -rf /var/lib/apt/lists/*

# Target libraries (arm64). Everything else comes from Dolphin's Externals (USE_SYSTEM_LIBS=OFF),
# because the phone lacks most of the optional system libraries.
# qt6-declarative-dev is for the QML frontend (stage 3, Source/Core/DolphinUT).
RUN apt-get update && apt-get install -y --no-install-recommends \
      qt6-base-dev:arm64 qt6-base-private-dev:arm64 qt6-svg-dev:arm64 \
      qt6-declarative-dev:arm64 \
      libegl-dev:arm64 libgl-dev:arm64 libgles-dev:arm64 \
      libx11-dev:arm64 libxi-dev:arm64 libxrandr-dev:arm64 \
      libwayland-dev:arm64 libxkbcommon-dev:arm64 \
      libevdev-dev:arm64 libudev-dev:arm64 libsystemd-dev:arm64 \
      libpulse-dev:arm64 libasound2-dev:arm64 libbluetooth-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*

# Click packaging (stage 5, ut/click/build-click.sh): `click build`, and the Qt 6 Wayland shell
# integration the phone lacks (stage 0), unpacked from UBports' arm64 packages into
# /opt/ut-bundle to be copied into the click: the xdg-shell plugin and libQt6WlShellIntegration.
RUN apt-get update && apt-get install -y --no-install-recommends click \
 && mkdir -p /tmp/debs /opt/ut-bundle && cd /tmp/debs \
 && apt-get download qt6-wayland:arm64 libqt6wlshellintegration6:arm64 \
 && for d in *.deb; do dpkg-deb -x "$d" /opt/ut-bundle; done \
 && ls /opt/ut-bundle/usr/lib/aarch64-linux-gnu/qt6/plugins/wayland-shell-integration/libxdg-shell.so \
       /opt/ut-bundle/usr/lib/aarch64-linux-gnu/libQt6WlShellIntegration.so.6 \
 && rm -rf /tmp/debs /var/lib/apt/lists/*

COPY aarch64-linux-gnu.cmake /opt/aarch64-linux-gnu.cmake
WORKDIR /src

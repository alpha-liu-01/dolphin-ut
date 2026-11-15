# CMake toolchain for the dolphin-ut-cross container: amd64 host, arm64 Ubuntu multiarch target.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Multiarch: target libraries live in /usr/lib/aarch64-linux-gnu, headers in /usr/include.
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu /)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")

# Qt 6 cross build: target Qt from the arm64 packages, host tools (moc, rcc, uic) from amd64.
set(QT_HOST_PATH /usr)
set(QT_HOST_PATH_CMAKE_DIR /usr/lib/x86_64-linux-gnu/cmake)

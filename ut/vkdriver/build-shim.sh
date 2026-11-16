#!/bin/sh
# Builds the stand-in libvndksupport.so (stage 4, vkdriver-shim.c), a bionic library for libhybris,
# without the Android NDK: the cross container's arm64 GCC with -nostdlib, linked against the
# phone's own bionic libraries, with libvndksupport's symbol versions (vkdriver-shim.map).
#   ut/vkdriver/build-shim.sh      build-ut/vkdriver/libvndksupport.so
# The bionic libraries are pulled from the phone over adb into build-ut/vkdriver/bionic/ once.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
out="$root/build-ut/vkdriver"
mkdir -p "$out/bionic"
for f in libc.so libdl.so libdl_android.so; do
  [ -f "$out/bionic/$f" ] || adb pull "/apex/com.android.runtime/lib64/bionic/$f" "$out/bionic/$f" >/dev/null
done
docker run --rm -u "$(id -u):$(id -g)" -v "$root:/src" -w /src dolphin-ut-cross \
  aarch64-linux-gnu-gcc -O2 -fPIC -shared -nostdlib -ffreestanding -fno-builtin \
    -fno-stack-protector -fno-asynchronous-unwind-tables -Wall -Wextra \
    -Wl,--hash-style=both -Wl,-soname,libvndksupport.so -Wl,--version-script=ut/vkdriver/vkdriver-shim.map -Wl,-z,max-page-size=4096 \
    -o build-ut/vkdriver/libvndksupport.so ut/vkdriver/vkdriver-shim.c \
    build-ut/vkdriver/bionic/libc.so build-ut/vkdriver/bionic/libdl.so \
    build-ut/vkdriver/bionic/libdl_android.so
ls -l "$out/libvndksupport.so"

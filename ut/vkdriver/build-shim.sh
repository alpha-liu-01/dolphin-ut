#!/bin/sh
# Builds the stand-in libvndksupport.so (stage 4, vkdriver-shim.c), a bionic library for libhybris,
# without the Android NDK: the cross container's arm64 GCC with -nostdlib, with libvndksupport's
# symbol versions (vkdriver-shim.map).
#   ut/vkdriver/build-shim.sh      build-ut/vkdriver/libvndksupport.so
# It links against stub libc.so, libdl.so and libdl_android.so (bionic-stubs.c) that carry only the
# imported names, sonames and bionic's symbol versions (LIBC, LIBDL_ANDROID); at run time libhybris
# resolves them in the phone's own libraries. So the build needs no files from a phone.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
out="$root/build-ut/vkdriver"
mkdir -p "$out/stubs"
docker run --rm -u "$(id -u):$(id -g)" -v "$root:/src" -w /src dolphin-ut-cross sh -eu -c '
  s=build-ut/vkdriver/stubs
  cc="aarch64-linux-gnu-gcc -O2 -fPIC -shared -nostdlib -ffreestanding -fno-builtin -fno-stack-protector"
  stub() {  # soname define version symbols...
    soname=$1 define=$2 version=$3; shift 3
    printf "%s {\n  global:\n" "$version" > "$s/$soname.map"
    for symbol in "$@"; do printf "    %s;\n" "$symbol" >> "$s/$soname.map"; done
    printf "  local:\n    *;\n};\n" >> "$s/$soname.map"
    $cc -D$define -Wl,-soname,$soname -Wl,--version-script=$s/$soname.map -o $s/$soname \
      ut/vkdriver/bionic-stubs.c
  }
  stub libc.so STUB_LIBC LIBC strncmp snprintf getpid write getenv strlen
  stub libdl.so STUB_LIBDL LIBC dlopen dlclose dlerror android_dlopen_ext
  stub libdl_android.so STUB_LIBDL_ANDROID LIBDL_ANDROID android_create_namespace android_get_exported_namespace
  $cc -fno-asynchronous-unwind-tables -Wall -Wextra \
    -Wl,--hash-style=both -Wl,-soname,libvndksupport.so -Wl,--version-script=ut/vkdriver/vkdriver-shim.map -Wl,-z,max-page-size=4096 \
    -o build-ut/vkdriver/libvndksupport.so ut/vkdriver/vkdriver-shim.c \
    $s/libc.so $s/libdl.so $s/libdl_android.so
'
ls -l "$out/libvndksupport.so"

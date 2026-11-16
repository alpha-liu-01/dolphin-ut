// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Link-time stand-ins for the bionic functions the shim imports (build-shim.sh). Only their names,
// sonames and symbol versions matter: at run time libhybris resolves them in the phone's own
// libc.so, libdl.so and libdl_android.so. build-shim.sh compiles this file three times, once per
// library, with STUB_LIBC, STUB_LIBDL or STUB_LIBDL_ANDROID.

#if defined(STUB_LIBC)
void strncmp(void) {}
void snprintf(void) {}
void getpid(void) {}
void write(void) {}
void getenv(void) {}
void strlen(void) {}
#elif defined(STUB_LIBDL)
void dlopen(void) {}
void dlclose(void) {}
void dlerror(void) {}
void android_dlopen_ext(void) {}
#elif defined(STUB_LIBDL_ANDROID)
void android_create_namespace(void) {}
void android_get_exported_namespace(void) {}
#endif

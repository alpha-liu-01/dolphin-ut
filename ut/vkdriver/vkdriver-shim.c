// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// A custom Vulkan driver under libhybris (stage 4, docs/ut-port/stage4-adreno.md).
//
// Android's Vulkan loader (/system/lib64/libvulkan.so), which libhybris runs inside the app, loads
// the vendor driver with android_load_sphal_library("vulkan.adreno.so") from libvndksupport.so,
// into libhybris' "sphal" namespace. That namespace permits only the vendor partitions and /data,
// which the user cannot write. Android's own route for another driver (GraphicsEnv's updatable
// driver) needs a "vndk" namespace that libhybris does not provide.
//
// This is a stand-in libvndksupport.so (same soname, the same three versioned functions). Put
// first in libhybris' search path (HYBRIS_LD_LIBRARY_PATH, which the default namespace also
// accepts libraries from), it is the libvndksupport every Android library in the process gets.
// It behaves like Android's, except that with DOLPHIN_UT_VK_DRIVER_DIR set, "vulkan.*" comes from
// <dir>/vulkan.adreno.so, loaded in a namespace of its own (the idea of libadrenotools).
//
// With DOLPHIN_UT_VK_SYSTEM_DIR set (the click for Ubuntu Touch 24.04-1.x sets /vendor/lib64/hw,
// stage 7), "vulkan.*" otherwise comes from <dir>/<the same name>, in the same kind of namespace:
// the phone's own driver, which 1.x's libhybris does not find through "sphal" (its namespaces
// lack the hw folder that 2.x's have).
//
// Built without the NDK (ut/vkdriver/build-shim.sh): -nostdlib against the phone's bionic libc,
// libdl and libdl_android, so only the few functions below may be used.

typedef unsigned long size_t;
typedef unsigned long uint64_t;
typedef long int64_t;

extern char* getenv(const char* name);
extern int strncmp(const char* a, const char* b, size_t n);
extern int snprintf(char* out, size_t size, const char* format, ...);
extern long write(int fd, const void* data, size_t size);
extern size_t strlen(const char* text);
extern int getpid(void);
extern void* dlopen(const char* filename, int flags);
extern int dlclose(void* handle);
extern char* dlerror(void);
extern void* android_dlopen_ext(const char* filename, int flags, const void* extinfo);
extern void* android_get_exported_namespace(const char* name);
extern void* android_create_namespace(const char* name, const char* ld_library_path,
                                      const char* default_library_path, uint64_t type,
                                      const char* permitted_when_isolated_path, void* parent);

#define ANDROID_DLEXT_USE_NAMESPACE 0x200
#define ANDROID_NAMESPACE_TYPE_SHARED 2

struct android_dlextinfo
{
  uint64_t flags;
  void* reserved_addr;
  size_t reserved_size;
  int relro_fd;
  int library_fd;
  int64_t library_fd_offset;
  void* library_namespace;
};

static void Log(const char* text)
{
  write(2, text, strlen(text));
}

static void* OpenIn(void* library_namespace, const char* name, int flags)
{
  struct android_dlextinfo extinfo = {0};
  extinfo.flags = ANDROID_DLEXT_USE_NAMESPACE;
  extinfo.library_namespace = library_namespace;
  return android_dlopen_ext(name, flags, &extinfo);
}

// As Android's libvndksupport: the first of "sphal", "vendor", "default" that exists.
static void* VendorNamespace(const char** name)
{
  static void* found;
  static const char* found_name;
  if (!found)
  {
    static const char* const names[] = {"sphal", "vendor", "default"};
    for (int i = 0; i < 3 && !found; ++i)
    {
      found = android_get_exported_namespace(names[i]);
      found_name = names[i];
    }
  }
  *name = found_name;
  return found;
}

// <dir>/<file> from a namespace that may load from <dir>. "Shared" makes it a copy of the
// default namespace (its loaded libraries and search paths), where the driver's dependencies
// (libc, liblog, libcutils, libhardware, libnativewindow, libsync, libz) are found. No
// android_link_namespaces: libhybris does not implement it, and the call traps in ld-android.so.
// One namespace per folder (custom driver or system driver), created on first use.
static void* LoadVulkanDriverFrom(void** driver_namespace, const char* dir, const char* file,
                                  const char* name, int flags)
{
  char line[1024];
  if (!*driver_namespace)
  {
    *driver_namespace = android_create_namespace("dolphin-ut vulkan driver", dir, dir,
                                                 ANDROID_NAMESPACE_TYPE_SHARED, 0, 0);
    if (!*driver_namespace)
    {
      snprintf(line, sizeof(line), "[vkdriver-shim] creating a namespace failed: %s\n", dlerror());
      Log(line);
      return 0;
    }
  }
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", dir, file);
  void* handle = OpenIn(*driver_namespace, path, flags);
  snprintf(line, sizeof(line), "[vkdriver-shim] %s for %s: %s\n", path, name,
           handle ? "loaded" : dlerror());
  Log(line);
  return handle;
}

int android_is_in_vendor_process(void)
{
  if (getpid() == 1)
    return 0;
  return android_get_exported_namespace("vndk") == 0;
}

void* android_load_sphal_library(const char* name, int flags)
{
  static void* custom_namespace;
  static void* system_namespace;
  const int vulkan = strncmp(name, "vulkan.", 7) == 0;
  const char* dir = getenv("DOLPHIN_UT_VK_DRIVER_DIR");
  if (vulkan && dir && dir[0])
  {
    void* handle = LoadVulkanDriverFrom(&custom_namespace, dir, "vulkan.adreno.so", name, flags);
    if (handle)
      return handle;
    Log("[vkdriver-shim] falling back to the system driver\n");
  }
  const char* system_dir = getenv("DOLPHIN_UT_VK_SYSTEM_DIR");
  if (vulkan && system_dir && system_dir[0])
  {
    void* handle = LoadVulkanDriverFrom(&system_namespace, system_dir, name, name, flags);
    if (handle)
      return handle;
  }
  const char* namespace_name;
  void* vendor_namespace = VendorNamespace(&namespace_name);
  if (!vendor_namespace)
    return dlopen(name, flags);
  void* handle = OpenIn(vendor_namespace, name, flags);
  if (!handle)
  {
    char line[1024];
    snprintf(line, sizeof(line), "[vkdriver-shim] could not load %s from %s: %s\n", name,
             namespace_name, dlerror());
    Log(line);
  }
  return handle;
}

int android_unload_sphal_library(void* handle)
{
  return dlclose(handle);
}

// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/VulkanDriver.h"

#include <cstdlib>
#include <cstring>

#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

namespace VulkanDriver
{
namespace
{
// libhybris' own default when HYBRIS_LD_LIBRARY_PATH is unset (its default namespace's
// ld_library_paths on the phone).
constexpr const char* HYBRIS_DEFAULT_PATH = "/android/system/lib64:/android/vendor/lib64";

// libhybris routes an Android library's pthread functions to glibc through its hook table,
// because bionic's and glibc's pthread types differ. For a pthread function missing from that
// table it returns a numbered fake address (-2, -3, ...), so a call crashes recognizably. Its
// table has no pthread_barrier_*, which Mesa's util_queue_finish() uses: Turnip crashed with
// SIGBUS at PC 0xfffffffffffffffe in vkDestroyDevice, every time a game stopped.
//
// libhybris asks an application's hook callback first (hybris_set_hook_callback). This one
// supplies glibc's barrier functions. The types match on arm64: pthread_barrier_t is 32 bytes on
// both (bionic: int64_t[4]; glibc: char[32], 8-aligned), and both return -1 to the serial thread.
// Mesa passes no attributes; glibc reads only an int from them, the size of bionic's.
static_assert(sizeof(pthread_barrier_t) == 32);

void* HookSymbol(const char* name, const char* /*requester*/)
{
  if (std::strncmp(name, "pthread_barrier_", 16) != 0)
    return nullptr;
  if (std::strcmp(name + 16, "init") == 0)
    return reinterpret_cast<void*>(&pthread_barrier_init);
  if (std::strcmp(name + 16, "wait") == 0)
    return reinterpret_cast<void*>(&pthread_barrier_wait);
  if (std::strcmp(name + 16, "destroy") == 0)
    return reinterpret_cast<void*>(&pthread_barrier_destroy);
  return nullptr;
}

bool AddHooks(std::string* error)
{
  // The same instance libhybris' EGL and Vulkan libraries use later (one soname, one copy).
  void* hybris = dlopen("libhybris-common.so.1", RTLD_NOW | RTLD_GLOBAL);
  using SetHookCallback = void (*)(void* (*)(const char*, const char*));
  const auto set_callback =
      hybris ? reinterpret_cast<SetHookCallback>(dlsym(hybris, "hybris_set_hook_callback")) :
               nullptr;
  if (!set_callback)
  {
    *error = std::string("libhybris has no hook callback: ") + (hybris ? "" : dlerror());
    return false;
  }
  set_callback(&HookSymbol);
  return true;
}
}  // namespace

bool Use(const std::string& folder, const std::string& shim_folder, std::string* error)
{
  const std::string driver = folder + "/vulkan.adreno.so";
  const std::string shim = shim_folder + "/libvndksupport.so";
  if (access(driver.c_str(), R_OK) != 0)
  {
    *error = driver + " not found";
    return false;
  }
  if (access(shim.c_str(), R_OK) != 0)
  {
    *error = shim + " not found";
    return false;
  }
  if (!AddHooks(error))
    return false;
  const char* current = std::getenv("HYBRIS_LD_LIBRARY_PATH");
  const std::string path =
      shim_folder + ":" + (current && *current ? current : HYBRIS_DEFAULT_PATH);
  setenv("HYBRIS_LD_LIBRARY_PATH", path.c_str(), 1);
  setenv("DOLPHIN_UT_VK_DRIVER_DIR", folder.c_str(), 1);
  return true;
}
}  // namespace VulkanDriver

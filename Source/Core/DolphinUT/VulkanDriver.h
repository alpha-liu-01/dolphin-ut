// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// A custom Vulkan driver (stage 4, docs/ut-port/stage4-adreno.md), as the Android app offers with
// libadrenotools, which needs Android's own linker and so cannot work under libhybris.
//
// Vulkan here runs Android's loader (/system/lib64/libvulkan.so) inside libhybris. It loads the
// vendor driver with android_load_sphal_library() from libvndksupport.so. ut/vkdriver builds a
// stand-in libvndksupport.so that loads <folder>/vulkan.adreno.so instead (DOLPHIN_UT_VK_DRIVER_DIR)
// and otherwise behaves like Android's. libhybris finds it first when its folder leads
// HYBRIS_LD_LIBRARY_PATH.
//
// libhybris reads that variable when it starts, which is when the first Android library is loaded
// (Qt's EGL, after main() begins), and Android's loader loads its driver once per process. So the
// driver is chosen at start, before QGuiApplication.
namespace VulkanDriver
{
// Uses the driver in FOLDER (vulkan.adreno.so) for this process, with the stand-in
// libvndksupport.so from SHIM_FOLDER. Returns false with a reason if a file is missing.
bool Use(const std::string& folder, const std::string& shim_folder, std::string* error);
}  // namespace VulkanDriver

// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

// Dolphin's hotkeys while a game runs (stage 6.2), as the Android app's HotkeyScheduler
// (Source/Android/jni/HotkeyScheduler.h): a thread polls the hotkeys mapped in Hotkeys.ini and
// handles the ones the Android app supports (screenshot, speed, volume, internal resolution, save
// states). It polls only while one of them is mapped, to save battery.
namespace HotkeyScheduler
{
// Around a game (MainAndroid's Run).
void Start();
void Stop();

// After the hotkey mapping changes.
void RefreshActiveHotkeys();

// The Android app's Hotkeys.setEnabled: off while the game is not in front.
void SetEnabled(bool enabled);

// The hotkeys handled, as HotkeyManager IDs (HK_*): the ones the hotkey page lists.
const std::vector<int>& GetSupportedHotkeys();
}  // namespace HotkeyScheduler

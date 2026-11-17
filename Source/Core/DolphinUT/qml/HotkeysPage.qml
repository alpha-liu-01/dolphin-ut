// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick

// Hotkeys (stage 6.2), as the Android app's hotkey settings (SettingsFragmentPresenter.
// addHotkeySettings): the device, Reset to Default, Clear and profiles, then the hotkeys the
// HotkeyScheduler handles, under their groups. They work while a game is in front.
MappingPage {
    mapping: controllers.mapping("hotkeys", 0)
    resetText: "Replace the hotkeys with Dolphin's defaults? They are keys on a keyboard: F1–F8 load states, Shift+F1–F8 save them, F9 takes a screenshot, Tab turns the speed limit off and on."
}

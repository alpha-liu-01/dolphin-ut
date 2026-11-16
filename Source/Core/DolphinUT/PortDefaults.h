// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QStringList>

// The port's defaults for this phone (stage 3.8, docs/ut-port/stage3.8-defaults.md): the settings
// where the port differs from Dolphin's desktop defaults, as the Android app does with
// `#if defined(ANDROID)` in Core. The port keeps Core unchanged and writes these values into the
// user's Dolphin.ini instead, once per user folder, and only where the user has not set the key
// already. Each default has a version ([UbuntuTouch] DefaultsVersion), so a default added later
// reaches existing user folders once too. Settings that change emulated behavior are not here.
namespace PortDefaults
{
// After UICommon::Init (the user's config is loaded). Returns what it set, for the log.
QStringList Apply();
}  // namespace PortDefaults

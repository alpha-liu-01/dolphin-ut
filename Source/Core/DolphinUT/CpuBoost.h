// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/Config/Config.h"

// The port's CPU boost (stage 3.0 benchmark, setting in 3.6): Ubuntu Touch has no game mode, and the
// CPU governor kept the big cores at about 1.2 GHz while the emulation threads ran just under its
// boost threshold. A minimum utilization clamp (uclamp_min) on the app's threads makes the scheduler
// clock them up and place them on the big cores; no root is needed. This is a host scheduling hint,
// not a change to the emulated console.
namespace CpuBoost
{
// 0 off, 512 medium, 1024 maximum (the uclamp scale). Dolphin.ini [UbuntuTouch] CPUBoost; per game
// through the game's settings like any other setting.
extern const Config::Info<int> UT_CPU_BOOST;

// Sets uclamp_min on every thread of the process (new threads inherit it from their creator, but
// threads made before a change do not, so it is applied to all of them). Returns the number of
// threads changed.
int Apply(int util_min);
}  // namespace CpuBoost

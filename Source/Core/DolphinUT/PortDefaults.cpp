// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/PortDefaults.h"

#include <algorithm>
#include <functional>

#include "Common/Config/Config.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/UISettings.h"

namespace PortDefaults
{
namespace
{
const Config::Info<int> UT_DEFAULTS_VERSION{
    {Config::System::Main, "UbuntuTouch", "DefaultsVersion"}, 0};

struct Default
{
  // The DefaultsVersion that introduced it.
  int version;
  const char* description;
  // Sets the value unless the user's Dolphin.ini has the key; returns whether it did.
  std::function<bool()> apply;
};

template <typename T>
bool SetIfUnset(const Config::Info<T>& info, const T& value)
{
  if (Config::GetLayer(Config::LayerType::Base)->Exists(info.GetLocation()))
    return false;
  Config::SetBase(info, value);
  return true;
}

// Version 1 (stage 3.8). Already the defaults without an entry here: the Vulkan backend (first in
// this build's backend list; OpenGL ES is the fallback, Emulator), the CPU boost at maximum
// ([UbuntuTouch] CPUBoost), the FPS display off, and Wii Remote 1 with a Nunchuk (as on Android).
const Default DEFAULTS[] = {
    // Dual core, as on Android: 79 % against 41 % of full speed in the 3.0 benchmark (user, 3.0).
    {1, "Core.CPUThread = True (dual core)",
     [] { return SetIfUnset(Config::MAIN_CPU_THREAD, true); }},
    // Save states in the in-game menu (user, 3.7; Android: off).
    {1, "Core.EnableSaveStates = True",
     [] { return SetIfUnset(Config::MAIN_ENABLE_SAVESTATES, true); }},
    // Covers from GameTDB, as on Android (user, 3.1).
    {1, "General.UseGameCovers = True",
     [] { return SetIfUnset(Config::MAIN_USE_GAME_COVERS, true); }},
};
}  // namespace

QStringList Apply()
{
  const int done = Config::GetBase(UT_DEFAULTS_VERSION);
  int latest = done;
  QStringList applied;
  for (const Default& entry : DEFAULTS)
  {
    latest = std::max(latest, entry.version);
    if (entry.version > done && entry.apply())
      applied.append(QString::fromUtf8(entry.description));
  }
  if (latest != done)
  {
    Config::SetBase(UT_DEFAULTS_VERSION, latest);
    Config::Save();
  }
  return applied;
}
}  // namespace PortDefaults

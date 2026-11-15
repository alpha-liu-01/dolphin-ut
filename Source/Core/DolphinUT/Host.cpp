// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The Host_* functions Dolphin's core calls into its frontend (Core/Host.h).

#include <memory>
#include <string>
#include <vector>

#include <QLocale>

#include "Core/Host.h"
#include "DolphinUT/Emulator.h"

std::vector<std::string> Host_GetPreferredLocales()
{
  std::vector<std::string> locales;
  for (const QString& language : QLocale::system().uiLanguages())
    locales.push_back(language.toStdString());
  return locales;
}

void Host_PPCSymbolsChanged()
{
}

void Host_PPCBreakpointsChanged()
{
}

bool Host_UIBlocksControllerState()
{
  return false;
}

void Host_Message(HostMessageID id)
{
  if (id == HostMessageID::WMUserStop)
    Emulator::RequestStopFromAnyThread();
}

void Host_UpdateTitle(const std::string& title)
{
  Emulator::LogFromAnyThread(QStringLiteral("title: ") + QString::fromStdString(title));
}

void Host_UpdateDisasmDialog()
{
}

void Host_JitCacheInvalidation()
{
}

void Host_JitProfileDataWiped()
{
}

void Host_RequestRenderWindowSize(int, int)
{
}

bool Host_RendererHasFocus()
{
  return Emulator::GameWindowHasFocus();
}

bool Host_RendererHasFullFocus()
{
  return Host_RendererHasFocus();
}

bool Host_RendererIsFullscreen()
{
  return true;
}

bool Host_TASInputHasFocus()
{
  return false;
}

void Host_YieldToUI()
{
}

void Host_TitleChanged()
{
}

void Host_UpdateDiscordClientID(const std::string&)
{
}

bool Host_UpdateDiscordPresenceRaw(const std::string&, const std::string&, const std::string&,
                                   const std::string&, const std::string&, const std::string&,
                                   const int64_t, const int64_t, const int, const int)
{
  return false;
}

std::unique_ptr<GBAHostInterface> Host_CreateGBAHost(std::weak_ptr<HW::GBA::Core>)
{
  return nullptr;
}

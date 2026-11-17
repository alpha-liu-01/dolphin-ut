// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/Achievements.h"

#include <QMetaObject>

#include <rcheevos/include/rc_error.h>

#include "Common/Config/Config.h"
#include "Core/AchievementManager.h"
#include "Core/Config/AchievementSettings.h"
#include "DolphinUT/Emulator.h"

Achievements::Achievements(QObject* parent) : QObject(parent)
{
  auto& manager = AchievementManager::GetInstance();
  // As MainAndroid's Initialize and DolphinQt's MainWindow: started with the app. It does nothing
  // unless achievements are enabled, and logs in with the stored token if there is one.
  manager.Init(nullptr);
  // Login results and account updates come from rcheevos' threads.
  m_login_hook = manager.login_event.Register([this](int result) {
    QMetaObject::invokeMethod(this, [this, result] {
      const bool was_logging_in = m_logging_in;
      m_logging_in = false;
      if (result == RC_OK)
      {
        // The token is in the base layer; keep it (the core does not save it).
        Config::Save();
      }
      Emulator::LogFromAnyThread(QStringLiteral("achievements: login %1")
                                     .arg(result == RC_OK ? QStringLiteral("ok") :
                                                            QString::fromUtf8(rc_error_str(result))));
      emit changed();
      if (was_logging_in)
      {
        emit loginFinished(result == RC_OK,
                           result == RC_OK ? QString() : QString::fromUtf8(rc_error_str(result)));
      }
    });
  });
  m_update_hook = manager.update_event.Register(
      [this](const AchievementManager::UpdatedItems&) {
        QMetaObject::invokeMethod(this, [this] { emit changed(); });
      });
}

Achievements::~Achievements()
{
  m_login_hook.reset();
  m_update_hook.reset();
  // As DolphinQt's MainWindow destructor.
  AchievementManager::GetInstance().Shutdown();
}

bool Achievements::IsLoggedIn() const
{
  return AchievementManager::GetInstance().HasAPIToken();
}

QString Achievements::GetUsername() const
{
  return QString::fromStdString(Config::Get(Config::RA_USERNAME));
}

QString Achievements::GetDisplayName() const
{
  const auto name = AchievementManager::GetInstance().GetPlayerDisplayName();
  return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

int Achievements::GetScore() const
{
  return static_cast<int>(AchievementManager::GetInstance().GetPlayerScore());
}

bool Achievements::IsHardcoreActive() const
{
  return AchievementManager::GetInstance().IsHardcoreModeActive();
}

void Achievements::SetEnabled(bool enabled)
{
  auto& manager = AchievementManager::GetInstance();
  if (enabled)
    manager.Init(nullptr);
  else
    manager.Shutdown();
  Emulator::LogFromAnyThread(QStringLiteral("achievements: %1").arg(enabled ? "on" : "off"));
  emit changed();
}

bool Achievements::login(const QString& username, const QString& password)
{
  if (m_logging_in || username.trimmed().isEmpty() || password.isEmpty() ||
      !Config::Get(Config::RA_ENABLED))
  {
    return false;
  }
  Config::SetBaseOrCurrent(Config::RA_USERNAME, username.trimmed().toStdString());
  Config::Save();
  m_logging_in = true;
  emit changed();
  Emulator::LogFromAnyThread(QStringLiteral("achievements: logging in"));
  AchievementManager::GetInstance().Login(password.toStdString());
  return true;
}

void Achievements::logout()
{
  AchievementManager::GetInstance().Logout();
  Config::Save();
  Emulator::LogFromAnyThread(QStringLiteral("achievements: logged out"));
  emit changed();
}

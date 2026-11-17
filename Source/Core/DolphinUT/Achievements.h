// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include "Common/HookableEvent.h"

// RetroAchievements (stage 6.7, docs/ut-port/stage6.7-achievements.md), as the Android app's
// achievement settings (SettingsFragmentPresenter.addAchievementSettings, LoginDialog,
// Source/Android/jni/AchievementAdapter.cpp): Dolphin's AchievementManager started with the app and
// on "Enable Achievements", stopped when that is turned off; logging in with a username and
// password (Dolphin keeps only the token it gets back), logging out. The achievements themselves,
// their notifications in the picture and hardcore mode's rules are Dolphin's.
class Achievements : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool loggedIn READ IsLoggedIn NOTIFY changed)
  Q_PROPERTY(bool loggingIn READ IsLoggingIn NOTIFY changed)
  Q_PROPERTY(QString username READ GetUsername NOTIFY changed)
  // The account's display name and score once the server answered (else "" and 0).
  Q_PROPERTY(QString displayName READ GetDisplayName NOTIFY changed)
  Q_PROPERTY(int score READ GetScore NOTIFY changed)
  Q_PROPERTY(bool hardcoreActive READ IsHardcoreActive NOTIFY changed)

public:
  explicit Achievements(QObject* parent = nullptr);
  ~Achievements() override;

  bool IsLoggedIn() const;
  bool IsLoggingIn() const { return m_logging_in; }
  QString GetUsername() const;
  QString GetDisplayName() const;
  int GetScore() const;
  bool IsHardcoreActive() const;

  // Settings → RetroAchievements → Enable Achievements (SettingsModel): start or stop Dolphin's
  // AchievementManager, as the presenter's AchievementModel.init / shutdown.
  void SetEnabled(bool enabled);

  // LoginDialog: the username is stored, the password goes to the server only.
  Q_INVOKABLE bool login(const QString& username, const QString& password);
  Q_INVOKABLE void logout();

signals:
  void changed();
  // message: "" on success, else why (rcheevos' error text).
  void loginFinished(bool ok, const QString& message);

private:
  bool m_logging_in = false;
  Common::EventHook m_login_hook;
  Common::EventHook m_update_hook;
};

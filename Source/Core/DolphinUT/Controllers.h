// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include "Common/HookableEvent.h"

class InputConfig;
namespace ControllerEmu
{
class Control;
class ControlGroup;
class ControlGroupContainer;
class EmulatedController;
}  // namespace ControllerEmu
namespace ciface::Core
{
class InputDetector;
}

// Controllers and mapping (stage 3.5, docs/ut-port/stage3.5-controllers.md), after the Android app's
// controller settings (SettingsFragmentPresenter.addGcPadSubSettings / addWiimoteSubSettings and
// the JNI in Source/Android/jni/Input): which controller each port has, and a mapping editor.
class Controllers : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QStringList devices READ GetDevices NOTIFY devicesChanged)

public:
  explicit Controllers(QObject* parent = nullptr);
  ~Controllers() override;

  // After UICommon::InitControllers: watches devices and maps a new gamepad where needed.
  void Start();
  // The phone's vibrator (hfd-service), for the "UbuntuTouch/0/Phone" device's Motor output: games'
  // rumble on the phone, as the Android app's "Device Sensors: Motor".
  void SetVibrator(std::function<void(int duration_ms)> vibrate);

  QStringList GetDevices() const { return m_devices; }

  // GameCube ports: the Android app's types (gcpadTypeEntries/Values: SI device numbers).
  Q_INVOKABLE QVariantList gcPortTypes() const;
  Q_INVOKABLE int gcPortType(int port) const;
  Q_INVOKABLE void setGcPortType(int port, int type);

  // Wii Remotes: source 0 none, 1 emulated, 2 real; extension 0 none, 1 Nunchuk, 2 Classic, …
  Q_INVOKABLE int wiimoteSource(int index) const;
  Q_INVOKABLE void setWiimoteSource(int index, int source);
  Q_INVOKABLE QStringList wiimoteExtensions() const;
  Q_INVOKABLE int wiimoteExtension(int index) const;
  Q_INVOKABLE void setWiimoteExtension(int index, int extension);

  // A mapping editor (owned by QML). kind: "gcpad", "wiimote" or "hotkeys" (stage 6.2: the hotkeys
  // HotkeyScheduler handles, as the Android app's hotkey settings); extension >= 0 edits that Wii
  // Remote extension's controls; group_types (ControllerEmu::GroupType numbers) filters the groups,
  // as the Android app splits the Wii Remote's settings into pages.
  Q_INVOKABLE QObject* mapping(const QString& kind, int index, int extension = -1,
                               const QVariantList& group_types = {});

signals:
  void devicesChanged();
  void portsChanged();
  void message(const QString& text);

private:
  void OnDevicesChanged();
  void AutoMap();

  QStringList m_devices;
  Common::EventHook m_devices_hook;
  std::function<void(int)> m_vibrate;
  QTimer m_rumble_timer;
};

class MappingController : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString title READ GetTitle CONSTANT)
  Q_PROPERTY(QString device READ GetDevice WRITE SetDevice NOTIFY changed)
  Q_PROPERTY(QVariantList groups READ GetGroups NOTIFY changed)
  Q_PROPERTY(bool detecting READ IsDetecting NOTIFY detectingChanged)
  Q_PROPERTY(QStringList profiles READ GetProfiles NOTIFY profilesChanged)

public:
  MappingController(InputConfig* config, ControllerEmu::EmulatedController* controller,
                    ControllerEmu::ControlGroupContainer* container, QString title,
                    std::vector<int> group_types);
  ~MappingController() override;

  // Only these controls, as {group index in the container, control index in the group}, in this
  // order; a group is listed again whenever the next control is in another group (the hotkeys, as
  // the Android app's addHotkeySettings). Such a list has no group settings.
  void ShowOnly(const std::vector<std::pair<int, int>>& controls);

  QString GetTitle() const { return m_title; }
  QString GetDevice() const;
  void SetDevice(const QString& device);
  QVariantList GetGroups() const;
  bool IsDetecting() const { return m_detector != nullptr; }
  QStringList GetProfiles() const;

  // For output controls (rumble), which cannot be detected: the chosen device's outputs and the
  // phone's motor, as expressions.
  Q_INVOKABLE QStringList outputs() const;

  // Waits for an input (3 s, at most 5 s, as the Android app's MotionAlertDialog) on the chosen
  // device, or on every device, and maps the control to it.
  Q_INVOKABLE void detect(int group, int control, bool all_devices);
  Q_INVOKABLE void cancelDetect();
  Q_INVOKABLE void setExpression(int group, int control, const QString& expression);
  Q_INVOKABLE void setSetting(int group, int setting, const QVariant& value);
  Q_INVOKABLE void setGroupEnabled(int group, bool enabled);
  // "Reset to Default": the gamepad layout for SDL gamepads, Dolphin's defaults otherwise.
  Q_INVOKABLE void resetToDefault();
  Q_INVOKABLE void clear();
  Q_INVOKABLE bool loadProfile(const QString& name);
  Q_INVOKABLE bool saveProfile(const QString& name);
  Q_INVOKABLE bool deleteProfile(const QString& name);

signals:
  void changed();
  void detectingChanged();
  void detected(int group, int control, const QString& expression);
  void profilesChanged();

private:
  void Save();
  void UpdateDetection();
  // The control shown at {group, control}, or nullptr.
  ControllerEmu::Control* GetControl(int group, int control) const;
  bool IsHotkeys() const;

  InputConfig* m_config;
  ControllerEmu::EmulatedController* m_controller;  // device, profiles, saving
  ControllerEmu::ControlGroupContainer* m_container;  // the groups (the controller or an extension)
  QString m_title;
  // The groups shown, each with the controls shown (indices in the group).
  std::vector<std::pair<ControllerEmu::ControlGroup*, std::vector<int>>> m_groups;
  bool m_filtered = false;

  std::unique_ptr<ciface::Core::InputDetector> m_detector;
  int m_detect_group = -1;
  int m_detect_control = -1;
  QTimer m_detect_timer;
};

namespace GamepadDefaults
{
// Maps an emulated controller (and a Wii Remote's Nunchuk and Classic Controller) to an SDL
// gamepad with SDL's standard button names. Only the groups these defaults cover are changed.
void Apply(ControllerEmu::EmulatedController* controller, bool wiimote, const std::string& device);
}  // namespace GamepadDefaults

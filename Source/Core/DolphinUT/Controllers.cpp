// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/Controllers.h"

#include <algorithm>
#include <chrono>
#include <map>

#include <QMetaObject>
#include <QVariantMap>

#include "Common/Config/Config.h"
#include "Common/FileSearch.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/StringUtil.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/WiimoteSettings.h"
#include "Core/Core.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "Core/HotkeyManager.h"
#include "Core/System.h"
#include "DolphinUT/Emulator.h"
#include "DolphinUT/HotkeyScheduler.h"
#include "InputCommon/ControllerEmu/Control/Control.h"
#include "InputCommon/ControllerEmu/ControlGroup/Attachments.h"
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerEmu/Setting/NumericSetting.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "InputCommon/ControllerInterface/MappingCommon.h"
#include "InputCommon/InputConfig.h"

namespace
{
using ControllerEmu::ControlGroup;
using ControllerEmu::EmulatedController;

// The phone's vibrator as an input device with one output, "Motor": rumble on the phone, as the
// Android app's "Android/0/Device Sensors:Motor 0". Games set it from the emulation thread; the UI
// thread turns the level into hfd-service vibrations (Controllers::m_rumble_timer).
std::atomic<double> s_phone_rumble = 0.0;

class PhoneDevice final : public ciface::Core::Device
{
public:
  class Motor final : public Output
  {
  public:
    std::string GetName() const override { return "Motor"; }
    void SetState(ControlState state) override { s_phone_rumble = state; }
  };

  PhoneDevice() { AddOutput(new Motor); }
  std::string GetName() const override { return "Phone"; }
  std::string GetSource() const override { return "UbuntuTouch"; }
};

constexpr const char* PHONE_DEVICE = "UbuntuTouch/0/Phone";

// The Android app's gcpadTypeEntries / gcpadTypeValues (SerialInterface::SIDevices numbers).
struct PortType
{
  int value;
  const char* name;
};
constexpr PortType GC_PORT_TYPES[] = {
    {0, "None"},       {6, "Standard Controller"}, {7, "Keyboard Controller"},
    {8, "Steering Wheel"}, {9, "Dance Mat"},       {10, "DK Bongos"},
    {11, "Triforce Baseboard"}, {12, "GameCube Adapter"},
};

ControllerEmu::Attachments* WiimoteAttachments(int index)
{
  return static_cast<ControllerEmu::Attachments*>(
      Wiimote::GetWiimoteGroup(index, WiimoteEmu::WiimoteGroup::Attachments));
}

QString DeviceName(const QString& device)
{
  // <source>/<id>/<name>
  return device.section(QLatin1Char('/'), 2);
}
}  // namespace

// ---------------------------------------------------------------------------------------------
// Gamepad defaults

namespace GamepadDefaults
{
namespace
{
using Table = std::map<std::string, std::string>;

// SDL3 gamepad names (InputCommon/ControllerInterface/SDL/SDLGamepad.h): positions, not labels, so
// the layout is the same on Xbox-, PlayStation- and Nintendo-style pads.
// GameCube: A bottom, B left, X right, Y top, as the controller's own layout; Z on the right
// shoulder; L/R on the triggers, digital and analog.
const Table GAMECUBE = {
    {"Buttons/A", "`Button S`"},         {"Buttons/B", "`Button W`"},
    {"Buttons/X", "`Button E`"},         {"Buttons/Y", "`Button N`"},
    {"Buttons/Z", "`Shoulder R`"},       {"Buttons/Start", "`Start`"},
    {"Main Stick/Up", "`Left Y+`"},      {"Main Stick/Down", "`Left Y-`"},
    {"Main Stick/Left", "`Left X-`"},    {"Main Stick/Right", "`Left X+`"},
    {"C-Stick/Up", "`Right Y+`"},        {"C-Stick/Down", "`Right Y-`"},
    {"C-Stick/Left", "`Right X-`"},      {"C-Stick/Right", "`Right X+`"},
    {"Triggers/L", "`Trigger L`"},       {"Triggers/R", "`Trigger R`"},
    {"Triggers/L-Analog", "`Trigger L`"}, {"Triggers/R-Analog", "`Trigger R`"},
    {"D-Pad/Up", "`Pad N`"},             {"D-Pad/Down", "`Pad S`"},
    {"D-Pad/Left", "`Pad W`"},           {"D-Pad/Right", "`Pad E`"},
    {"Rumble/Motor", "`Motor L`|`Motor R`"},
};

// Wii Remote: A bottom, B on the right trigger (where it is on the remote), 1 and 2 left and right,
// −/+/Home on Back/Start/Guide, the pointer on the right stick, shake on the top button.
const Table WIIMOTE = {
    {"Buttons/A", "`Button S`"},   {"Buttons/B", "`Trigger R`"}, {"Buttons/1", "`Button W`"},
    {"Buttons/2", "`Button E`"},   {"Buttons/-", "`Back`"},      {"Buttons/+", "`Start`"},
    {"Buttons/Home", "`Guide`"},   {"D-Pad/Up", "`Pad N`"},      {"D-Pad/Down", "`Pad S`"},
    {"D-Pad/Left", "`Pad W`"},     {"D-Pad/Right", "`Pad E`"},   {"IR/Up", "`Right Y+`"},
    {"IR/Down", "`Right Y-`"},     {"IR/Left", "`Right X-`"},    {"IR/Right", "`Right X+`"},
    {"Shake/X", "`Button N`"},     {"Shake/Y", "`Button N`"},    {"Shake/Z", "`Button N`"},
    {"Rumble/Motor", "`Motor L`|`Motor R`"},
};

// Nunchuk: the left stick, C on the left shoulder, Z on the left trigger, shake on the stick press.
const Table NUNCHUK = {
    {"Buttons/C", "`Shoulder L`"}, {"Buttons/Z", "`Trigger L`"}, {"Stick/Up", "`Left Y+`"},
    {"Stick/Down", "`Left Y-`"},   {"Stick/Left", "`Left X-`"},  {"Stick/Right", "`Left X+`"},
    {"Shake/X", "`Thumb L`"},      {"Shake/Y", "`Thumb L`"},     {"Shake/Z", "`Thumb L`"},
};

// Classic Controller: Nintendo's layout (a right, b bottom, x top, y left), L/R on the triggers,
// ZL/ZR on the shoulders.
const Table CLASSIC = {
    {"Buttons/A", "`Button E`"},        {"Buttons/B", "`Button S`"},
    {"Buttons/X", "`Button N`"},        {"Buttons/Y", "`Button W`"},
    {"Buttons/ZL", "`Shoulder L`"},     {"Buttons/ZR", "`Shoulder R`"},
    {"Buttons/-", "`Back`"},            {"Buttons/+", "`Start`"},
    {"Buttons/Home", "`Guide`"},        {"Left Stick/Up", "`Left Y+`"},
    {"Left Stick/Down", "`Left Y-`"},   {"Left Stick/Left", "`Left X-`"},
    {"Left Stick/Right", "`Left X+`"},  {"Right Stick/Up", "`Right Y+`"},
    {"Right Stick/Down", "`Right Y-`"}, {"Right Stick/Left", "`Right X-`"},
    {"Right Stick/Right", "`Right X+`"}, {"Triggers/L", "`Trigger L`"},
    {"Triggers/R", "`Trigger R`"},      {"Triggers/L-Analog", "`Trigger L`"},
    {"Triggers/R-Analog", "`Trigger R`"}, {"D-Pad/Up", "`Pad N`"},
    {"D-Pad/Down", "`Pad S`"},          {"D-Pad/Left", "`Pad W`"},
    {"D-Pad/Right", "`Pad E`"},
};

void ApplyTable(ControllerEmu::ControlGroupContainer* controller, const Table& table)
{
  for (const auto& group : controller->groups)
  {
    const std::string prefix = group->name + "/";
    const bool covered = std::ranges::any_of(
        table, [&](const auto& entry) { return entry.first.starts_with(prefix); });
    if (!covered)
      continue;
    // The whole group: controls the table does not name are cleared (keyboard defaults and the
    // like make no sense on a gamepad).
    for (const auto& control : group->controls)
    {
      const auto it = table.find(prefix + control->name);
      control->control_ref->SetExpression(it != table.end() ? it->second : "");
    }
  }
}
}  // namespace

void Apply(EmulatedController* controller, bool wiimote, const std::string& device)
{
  const auto lock = EmulatedController::GetStateLock();
  controller->SetDefaultDevice(device);
  if (!wiimote)
  {
    ApplyTable(controller, GAMECUBE);
  }
  else
  {
    ApplyTable(controller, WIIMOTE);
    for (const auto& group : controller->groups)
    {
      if (group->type != ControllerEmu::GroupType::Attachments)
        continue;
      for (const auto& attachment :
           static_cast<ControllerEmu::Attachments*>(group.get())->GetAttachmentList())
      {
        if (attachment->GetName() == "Nunchuk")
          ApplyTable(attachment.get(), NUNCHUK);
        else if (attachment->GetName() == "Classic")
          ApplyTable(attachment.get(), CLASSIC);
      }
    }
  }
  controller->UpdateReferences(g_controller_interface);
}
}  // namespace GamepadDefaults

// ---------------------------------------------------------------------------------------------
// Controllers

Controllers::Controllers(QObject* parent) : QObject(parent)
{
}

Controllers::~Controllers() = default;

void Controllers::SetVibrator(std::function<void(int)> vibrate)
{
  m_vibrate = std::move(vibrate);
  // While a game asks for rumble, short vibrations follow each other (hfd-service has no "until
  // stopped"); they end within one period of the rumble stopping.
  m_rumble_timer.setInterval(60);
  connect(&m_rumble_timer, &QTimer::timeout, this, [this] {
    if (m_vibrate && s_phone_rumble > 0.25)
      m_vibrate(80);
  });
  m_rumble_timer.start();
}

void Controllers::Start()
{
  // Hotkeys (stage 6.2): none mapped at first, as in the Android app (HotkeyManager::LoadDefaults
  // maps nothing there), not desktop Dolphin's keyboard keys. A phone seldom has a keyboard, and
  // the hotkey thread polls during games only while a hotkey is mapped (1.4 % of a core here).
  // Reset to Default on the hotkey page gives Dolphin's keyboard keys.
  InputConfig* const hotkeys = HotkeyManagerEmu::GetConfig();
  if (!File::Exists(File::GetUserPath(D_CONFIG_IDX) + "Hotkeys.ini") &&
      !hotkeys->ControllersNeedToBeCreated())
  {
    EmulatedController* const controller = hotkeys->GetController(0);
    {
      const auto lock = EmulatedController::GetStateLock();
      const std::string device = controller->GetDefaultDevice().ToString();
      Common::IniFile::Section empty;
      controller->LoadConfig(&empty);
      controller->SetDefaultDevice(device);
      controller->UpdateReferences(g_controller_interface);
    }
    hotkeys->SaveConfig();
    Emulator::LogFromAnyThread(QStringLiteral("hotkeys: none mapped (first start)"));
  }

  m_devices_hook = g_controller_interface.RegisterDevicesChangedCallback([this] {
    QMetaObject::invokeMethod(this, [this] { OnDevicesChanged(); }, Qt::QueuedConnection);
  });
  OnDevicesChanged();
}

void Controllers::OnDevicesChanged()
{
  // The phone device is not from an input backend, so a device refresh drops it: add it again.
  const auto present = g_controller_interface.FindDevice(
      ciface::Core::DeviceQualifier("UbuntuTouch", 0, "Phone"));
  if (!present)
  {
    g_controller_interface.AddDevice(std::make_shared<PhoneDevice>());
    return;  // AddDevice reports the change again
  }

  QStringList devices;
  for (const std::string& device : g_controller_interface.GetAllDeviceStrings())
    devices.append(QString::fromStdString(device));
  if (devices != m_devices)
  {
    m_devices = devices;
    Emulator::LogFromAnyThread(QStringLiteral("input devices: %1").arg(devices.join(QStringLiteral(", "))));
    emit devicesChanged();
  }
  AutoMap();
}

void Controllers::AutoMap()
{
  // A gamepad that appears is mapped to GameCube port 1 and Wii Remote 1 if they have no usable
  // device: none connected, or the touchscreen (desktop Dolphin picks the first device it finds,
  // which on this phone was the touchscreen). A port mapped to a connected gamepad or keyboard is
  // left alone.
  const auto gamepad = std::ranges::find_if(
      m_devices, [](const QString& device) { return device.startsWith(QLatin1String("SDL/")); });
  if (gamepad == m_devices.end())
    return;

  const auto usable = [this](const QString& device) {
    if (device.isEmpty() || !m_devices.contains(device))
      return false;
    const QString name = DeviceName(device).toLower();
    return !name.contains(QLatin1String("touch")) && !name.endsWith(QLatin1String("_ts")) &&
           !device.startsWith(QLatin1String("XInput2/"));
  };

  const std::string device = gamepad->toStdString();
  if (Config::Get(Config::GetInfoForSIDevice(0)) == SerialInterface::SIDEVICE_GC_CONTROLLER)
  {
    EmulatedController* pad = Pad::GetConfig()->GetController(0);
    const QString current = QString::fromStdString(pad->GetDefaultDevice().ToString());
    if (!usable(current))
    {
      GamepadDefaults::Apply(pad, false, device);
      Pad::GetConfig()->SaveConfig();
      emit message(QStringLiteral("GameCube Controller 1 mapped to %1").arg(DeviceName(*gamepad)));
      Emulator::LogFromAnyThread(QStringLiteral("controllers: GameCube port 1 (was %1) mapped to %2")
                                     .arg(current, *gamepad));
    }
  }
  if (Config::Get(Config::GetInfoForWiimoteSource(0)) == WiimoteSource::Emulated)
  {
    EmulatedController* wiimote = Wiimote::GetConfig()->GetController(0);
    const QString current = QString::fromStdString(wiimote->GetDefaultDevice().ToString());
    if (!usable(current))
    {
      GamepadDefaults::Apply(wiimote, true, device);
      Wiimote::GetConfig()->SaveConfig();
      emit message(QStringLiteral("Wii Remote 1 mapped to %1").arg(DeviceName(*gamepad)));
      Emulator::LogFromAnyThread(QStringLiteral("controllers: Wii Remote 1 (was %1) mapped to %2")
                                     .arg(current, *gamepad));
    }
  }
}

QVariantList Controllers::gcPortTypes() const
{
  QVariantList types;
  for (const PortType& type : GC_PORT_TYPES)
    types.append(QVariantMap{{QStringLiteral("value"), type.value},
                             {QStringLiteral("name"), QString::fromLatin1(type.name)}});
  return types;
}

int Controllers::gcPortType(int port) const
{
  return static_cast<int>(Config::Get(Config::GetInfoForSIDevice(port)));
}

void Controllers::setGcPortType(int port, int type)
{
  const auto device = static_cast<SerialInterface::SIDevices>(type);
  Config::SetBaseOrCurrent(Config::GetInfoForSIDevice(port), device);
  Config::Save();
  emit portsChanged();
}

int Controllers::wiimoteSource(int index) const
{
  return static_cast<int>(Config::Get(Config::GetInfoForWiimoteSource(index)));
}

void Controllers::setWiimoteSource(int index, int source)
{
  Config::SetBaseOrCurrent(Config::GetInfoForWiimoteSource(index), WiimoteSource(source));
  Config::Save();
  emit portsChanged();
}

QStringList Controllers::wiimoteExtensions() const
{
  // The Android app's wiimoteExtensionsEntries.
  return {QStringLiteral("None"),   QStringLiteral("Nunchuk"), QStringLiteral("Classic"),
          QStringLiteral("Guitar"), QStringLiteral("Drums"),   QStringLiteral("Turntable")};
}

int Controllers::wiimoteExtension(int index) const
{
  return static_cast<int>(WiimoteAttachments(index)->GetSelectedAttachment());
}

void Controllers::setWiimoteExtension(int index, int extension)
{
  {
    const auto lock = EmulatedController::GetStateLock();
    WiimoteAttachments(index)->SetSelectedAttachment(static_cast<u32>(extension));
  }
  Wiimote::GetConfig()->SaveConfig();
  emit portsChanged();
}

QObject* Controllers::mapping(const QString& kind, int index, int extension,
                              const QVariantList& group_types)
{
  if (kind == QLatin1String("hotkeys"))
  {
    InputConfig* config = HotkeyManagerEmu::GetConfig();
    auto* hotkeys = static_cast<HotkeyManager*>(config->GetController(0));
    auto* mapping = new MappingController(config, hotkeys, hotkeys, QStringLiteral("Hotkeys"), {});
    std::vector<std::pair<int, int>> shown;
    for (const int hotkey : HotkeyScheduler::GetSupportedHotkeys())
    {
      const int group = hotkeys->FindGroupByID(hotkey);
      shown.emplace_back(group, hotkeys->GetIndexForGroup(group, hotkey));
    }
    mapping->ShowOnly(shown);
    return mapping;
  }

  const bool wiimote = kind == QLatin1String("wiimote");
  InputConfig* config = wiimote ? Wiimote::GetConfig() : Pad::GetConfig();
  EmulatedController* controller = config->GetController(index);
  ControllerEmu::ControlGroupContainer* container = controller;
  QString title = wiimote ? QStringLiteral("Wii Remote %1").arg(index + 1) :
                            QStringLiteral("GameCube Controller %1").arg(index + 1);
  if (wiimote && extension >= 0)
  {
    const auto& list = WiimoteAttachments(index)->GetAttachmentList();
    if (extension < static_cast<int>(list.size()))
    {
      container = list[extension].get();
      title += QStringLiteral(" – %1").arg(QString::fromStdString(container->GetDisplayName()));
    }
  }
  std::vector<int> types;
  for (const QVariant& type : group_types)
    types.push_back(type.toInt());
  // No parent: QML owns the object it is handed.
  return new MappingController(config, controller, container, title, std::move(types));
}

// ---------------------------------------------------------------------------------------------
// MappingController

MappingController::MappingController(InputConfig* config, EmulatedController* controller,
                                     ControllerEmu::ControlGroupContainer* container, QString title,
                                     std::vector<int> group_types)
    : m_config(config), m_controller(controller), m_container(container), m_title(std::move(title))
{
  for (const auto& group : m_container->groups)
  {
    if (group_types.empty() ||
        std::ranges::find(group_types, static_cast<int>(group->type)) != group_types.end())
    {
      std::vector<int> controls(group->controls.size());
      for (std::size_t i = 0; i < controls.size(); ++i)
        controls[i] = static_cast<int>(i);
      m_groups.emplace_back(group.get(), std::move(controls));
    }
  }
  m_detect_timer.setInterval(20);
  connect(&m_detect_timer, &QTimer::timeout, this, &MappingController::UpdateDetection);
}

MappingController::~MappingController() = default;

void MappingController::ShowOnly(const std::vector<std::pair<int, int>>& controls)
{
  m_groups.clear();
  m_filtered = true;
  for (const auto& [group_index, control_index] : controls)
  {
    if (group_index < 0 || group_index >= static_cast<int>(m_container->groups.size()))
      continue;
    ControlGroup* group = m_container->groups[group_index].get();
    if (control_index < 0 || control_index >= static_cast<int>(group->controls.size()))
      continue;
    if (m_groups.empty() || m_groups.back().first != group)
      m_groups.emplace_back(group, std::vector<int>{});
    m_groups.back().second.push_back(control_index);
  }
  emit changed();
}

ControllerEmu::Control* MappingController::GetControl(int group, int control) const
{
  if (group < 0 || group >= static_cast<int>(m_groups.size()))
    return nullptr;
  const auto& [control_group, shown] = m_groups[group];
  if (control < 0 || control >= static_cast<int>(shown.size()))
    return nullptr;
  return control_group->controls[shown[control]].get();
}

bool MappingController::IsHotkeys() const
{
  return m_config == HotkeyManagerEmu::GetConfig();
}

QString MappingController::GetDevice() const
{
  return QString::fromStdString(m_controller->GetDefaultDevice().ToString());
}

void MappingController::SetDevice(const QString& device)
{
  {
    const auto lock = EmulatedController::GetStateLock();
    m_controller->SetDefaultDevice(device.toStdString());
    m_controller->UpdateReferences(g_controller_interface);
  }
  Save();
  emit changed();
}

QVariantList MappingController::GetGroups() const
{
  QVariantList groups;
  for (const auto& [group, shown] : m_groups)
  {
    QVariantList controls;
    for (const int index : shown)
    {
      const auto& control = group->controls[index];
      controls.append(QVariantMap{
          {QStringLiteral("name"), QString::fromStdString(control->ui_name)},
          {QStringLiteral("expression"),
           QString::fromStdString(control->control_ref->GetExpression())},
          {QStringLiteral("input"), control->control_ref->IsInput()},
      });
    }
    QVariantList settings;
    for (const auto& setting : group->numeric_settings)
    {
      if (m_filtered)
        break;
      QVariantMap entry{
          {QStringLiteral("name"), QString::fromUtf8(setting->GetUIName())},
          {QStringLiteral("suffix"), QString::fromUtf8(setting->GetUISuffix())},
      };
      switch (setting->GetType())
      {
      case ControllerEmu::SettingType::Bool:
      {
        auto* s = static_cast<ControllerEmu::NumericSetting<bool>*>(setting.get());
        entry[QStringLiteral("type")] = QStringLiteral("bool");
        entry[QStringLiteral("value")] = s->GetValue();
        break;
      }
      case ControllerEmu::SettingType::Double:
      {
        auto* s = static_cast<ControllerEmu::NumericSetting<double>*>(setting.get());
        entry[QStringLiteral("type")] = QStringLiteral("double");
        entry[QStringLiteral("value")] = s->GetValue();
        entry[QStringLiteral("min")] = s->GetMinValue();
        entry[QStringLiteral("max")] = s->GetMaxValue();
        break;
      }
      case ControllerEmu::SettingType::Int:
      {
        auto* s = static_cast<ControllerEmu::NumericSetting<int>*>(setting.get());
        entry[QStringLiteral("type")] = QStringLiteral("int");
        entry[QStringLiteral("value")] = s->GetValue();
        entry[QStringLiteral("min")] = s->GetMinValue();
        entry[QStringLiteral("max")] = s->GetMaxValue();
        break;
      }
      }
      settings.append(entry);
    }
    groups.append(QVariantMap{
        {QStringLiteral("name"), QString::fromStdString(group->ui_name)},
        {QStringLiteral("type"), static_cast<int>(group->type)},
        {QStringLiteral("canDisable"), group->HasEnabledSetting()},
        {QStringLiteral("enabled"), group->enabled.GetValue()},
        {QStringLiteral("controls"), controls},
        {QStringLiteral("settings"), settings},
    });
  }
  return groups;
}

void MappingController::Save()
{
  m_config->SaveConfig();
  // The scheduler polls only while a hotkey is mapped.
  if (IsHotkeys())
    HotkeyScheduler::RefreshActiveHotkeys();
}

void MappingController::setExpression(int group, int control, const QString& expression)
{
  ControllerEmu::Control* const target = GetControl(group, control);
  if (!target)
    return;
  {
    const auto lock = EmulatedController::GetStateLock();
    target->control_ref->SetExpression(expression.toStdString());
    m_controller->UpdateReferences(g_controller_interface);
  }
  Save();
  emit changed();
}

void MappingController::setSetting(int group, int setting, const QVariant& value)
{
  if (group < 0 || group >= static_cast<int>(m_groups.size()))
    return;
  auto& settings = m_groups[group].first->numeric_settings;
  if (setting < 0 || setting >= static_cast<int>(settings.size()))
    return;
  {
    const auto lock = EmulatedController::GetStateLock();
    auto* base = settings[setting].get();
    switch (base->GetType())
    {
    case ControllerEmu::SettingType::Bool:
      static_cast<ControllerEmu::NumericSetting<bool>*>(base)->SetValue(value.toBool());
      break;
    case ControllerEmu::SettingType::Double:
      static_cast<ControllerEmu::NumericSetting<double>*>(base)->SetValue(value.toDouble());
      break;
    case ControllerEmu::SettingType::Int:
      static_cast<ControllerEmu::NumericSetting<int>*>(base)->SetValue(value.toInt());
      break;
    }
  }
  Save();
  emit changed();
}

void MappingController::setGroupEnabled(int group, bool enabled)
{
  if (group < 0 || group >= static_cast<int>(m_groups.size()))
    return;
  {
    const auto lock = EmulatedController::GetStateLock();
    m_groups[group].first->enabled.SetValue(enabled);
  }
  Save();
  emit changed();
}

void MappingController::resetToDefault()
{
  const std::string device = m_controller->GetDefaultDevice().ToString();
  const bool wiimote = m_config == Wiimote::GetConfig();
  if (IsHotkeys())
  {
    // Dolphin's hotkey defaults are keyboard keys (desktop Dolphin's), so they keep the device
    // Dolphin picks for them, the keyboard (Wayland/0/Keyboard Mouse on the phone).
    const auto lock = EmulatedController::GetStateLock();
    m_controller->LoadDefaults(g_controller_interface);
    m_controller->UpdateReferences(g_controller_interface);
  }
  else if (device.starts_with("SDL/"))
  {
    GamepadDefaults::Apply(m_controller, wiimote, device);
  }
  else
  {
    // Dolphin's own defaults (the keyboard on desktop), as the Android app's "Reset to Default".
    const auto lock = EmulatedController::GetStateLock();
    m_controller->LoadDefaults(g_controller_interface);
    m_controller->SetDefaultDevice(device);
    m_controller->UpdateReferences(g_controller_interface);
  }
  Save();
  emit changed();
}

void MappingController::clear()
{
  // As the Android app's "Clear": an empty configuration, keeping the device.
  const std::string device = m_controller->GetDefaultDevice().ToString();
  {
    const auto lock = EmulatedController::GetStateLock();
    Common::IniFile::Section section;
    m_controller->LoadConfig(&section);
    m_controller->SetDefaultDevice(device);
    m_controller->UpdateReferences(g_controller_interface);
  }
  Save();
  emit changed();
}

QStringList MappingController::outputs() const
{
  QStringList outputs;
  const ciface::Core::DeviceQualifier& device = m_controller->GetDefaultDevice();
  if (const auto found = g_controller_interface.FindDevice(device))
  {
    for (const auto* output : found->Outputs())
      outputs.append(QStringLiteral("`%1`").arg(QString::fromStdString(output->GetName())));
  }
  if (device.ToString() != PHONE_DEVICE)
    outputs.append(QStringLiteral("`%1:Motor`").arg(QString::fromLatin1(PHONE_DEVICE)));
  return outputs;
}

QStringList MappingController::GetProfiles() const
{
  QStringList profiles;
  // No folder until the first profile is saved (DoFileSearch would log an error).
  if (!File::IsDirectory(m_config->GetUserProfileDirectoryPath()))
    return profiles;
  for (const std::string& path :
       Common::DoFileSearch({m_config->GetUserProfileDirectoryPath()}, {".ini"}))
  {
    std::string name;
    SplitPath(path, nullptr, &name, nullptr);
    profiles.append(QString::fromStdString(name));
  }
  profiles.sort(Qt::CaseInsensitive);
  return profiles;
}

bool MappingController::loadProfile(const QString& name)
{
  Common::IniFile ini;
  if (!ini.Load(m_config->GetUserProfileDirectoryPath() + name.toStdString() + ".ini"))
    return false;
  {
    const auto lock = EmulatedController::GetStateLock();
    m_controller->LoadConfig(ini.GetOrCreateSection("Profile"));
    m_controller->UpdateReferences(g_controller_interface);
  }
  Save();
  emit changed();
  return true;
}

bool MappingController::saveProfile(const QString& name)
{
  const QString clean = name.trimmed();
  if (clean.isEmpty() || clean.contains(QLatin1Char('/')))
    return false;
  const std::string directory = m_config->GetUserProfileDirectoryPath();
  File::CreateFullPath(directory);
  Common::IniFile ini;
  m_controller->SaveConfig(ini.GetOrCreateSection("Profile"));
  const bool saved = ini.Save(directory + clean.toStdString() + ".ini");
  emit profilesChanged();
  return saved;
}

bool MappingController::deleteProfile(const QString& name)
{
  const bool deleted =
      File::Delete(m_config->GetUserProfileDirectoryPath() + name.toStdString() + ".ini");
  emit profilesChanged();
  return deleted;
}

void MappingController::detect(int group, int control, bool all_devices)
{
  cancelDetect();
  if (!GetControl(group, control))
    return;
  m_detect_group = group;
  m_detect_control = control;
  const std::vector<std::string> devices =
      all_devices ? g_controller_interface.GetAllDeviceStrings() :
                    std::vector<std::string>{m_controller->GetDefaultDevice().ToString()};
  m_detector = std::make_unique<ciface::Core::InputDetector>();
  m_detector->Start(g_controller_interface, devices);
  m_detect_timer.start();
  emit detectingChanged();
}

void MappingController::cancelDetect()
{
  if (!m_detector)
    return;
  m_detect_timer.stop();
  m_detector.reset();
  emit detectingChanged();
}

void MappingController::UpdateDetection()
{
  if (!m_detector)
    return;
  // Without a running game, nothing polls the devices: do it here (as DolphinQt's mapping window).
  // Also with the game paused (stage 6.1: Settings from the in-game menu pause it).
  if (Core::GetState(Core::System::GetInstance()) != Core::State::Running)
    g_controller_interface.UpdateInput();

  // The Android app's InputDetector timings: wait 3 s for an input, at most 5 s in all.
  using namespace std::chrono_literals;
  m_detector->Update(3s, 0ms, 5s);
  if (!m_detector->IsComplete())
    return;

  auto results = m_detector->TakeResults();
  ciface::MappingCommon::RemoveSpuriousTriggerCombinations(&results);
  const std::string expression = ciface::MappingCommon::BuildExpression(
      results, m_controller->GetDefaultDevice(), ciface::MappingCommon::Quote::On);
  m_detect_timer.stop();
  m_detector.reset();
  emit detectingChanged();
  if (!expression.empty())
    setExpression(m_detect_group, m_detect_control, QString::fromStdString(expression));
  emit detected(m_detect_group, m_detect_control, QString::fromStdString(expression));
}

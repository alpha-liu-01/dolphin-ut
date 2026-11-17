// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <QObject>
#include <QTimer>

class QLocalSocket;

// The phone's accelerometer and gyroscope (stage 6.6, docs/ut-port/stage6.6-motion.md), for the
// "UbuntuTouch/0/Phone" input device: Wii Remote motion from the phone, as the Android app's
// "Device Sensors" (DolphinSensorEventListener).
//
// Ubuntu Touch serves the sensors through sensorfwd (Sailfish's sensor daemon, over the Android
// sensor HAL). Its Qt API is Qt 5 only, so this speaks its protocol directly, as libsensorclient
// does: D-Bus (system bus, com.nokia.SensorService) to load, request and start a sensor, and the
// socket /run/sensord.sock for the samples (the session id, a tag byte, then batches of
// [uint32 count][count × {u64 time, float x, y, z}]). Accelerometer in milli-g, gyroscope in
// milli-degrees per second; converted to m/s² and rad/s, Android's units, which Dolphin's IMU
// groups expect.
//
// The sensors run only while something reads them (a mapped Wii Remote during a game), as Android
// suspends unread sensors: started at the first read, stopped a few seconds after the last.
class PhoneSensors : public QObject
{
  Q_OBJECT

public:
  // Axes in Android's screen coordinates (x right, y up, z out of the screen), already rotated for
  // a landscape game as Android rotates them for the display rotation.
  enum Sensor
  {
    Accelerometer,
    Gyroscope,
    SensorCount
  };

  explicit PhoneSensors(QObject* parent = nullptr);
  ~PhoneSensors() override;

  // The one instance (the phone device's inputs read it from the emulation threads).
  static PhoneSensors* Instance();

  // From any thread: the latest value on an axis (0 x, 1 y, 2 z), and a note that it was wanted.
  double Read(Sensor sensor, int axis);

  // Whether the game window is landscape (Emulator, on resize): the axes then turn with the side
  // the phone's top edge is on, which gravity tells (Lomiri does not report its rotation).
  void SetLandscape(bool landscape) { m_landscape = landscape; }

  // For tests and the log: samples received since start, per sensor.
  int Samples(Sensor sensor) const { return m_samples[sensor].load(); }

private:
  struct Session;

  void Check();
  bool Start(Sensor sensor);
  void Stop(Sensor sensor);
  void OnData(Sensor sensor);
  void Store(Sensor sensor, float x, float y, float z);

  std::array<std::unique_ptr<Session>, SensorCount> m_sessions;
  std::array<std::array<std::atomic<double>, 3>, SensorCount> m_values{};
  std::array<std::atomic<int>, SensorCount> m_samples{};
  std::atomic<qint64> m_last_read_ms = 0;
  std::atomic<bool> m_landscape = false;
  // The landscape side: +1 top edge to the left (Android ROTATION_90), -1 to the right (270).
  std::atomic<int> m_side = 1;
  QTimer m_check_timer;
  bool m_unavailable = false;
};

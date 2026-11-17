// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/PhoneSensors.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <numbers>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QLocalSocket>

#include "DolphinUT/Emulator.h"

namespace
{
PhoneSensors* s_instance = nullptr;

constexpr const char* SERVICE = "com.nokia.SensorService";
constexpr const char* MANAGER_PATH = "/SensorManager";
constexpr const char* MANAGER_INTERFACE = "local.SensorManager";
constexpr const char* SOCKET_PATH = "/run/sensord.sock";

struct SensorInfo
{
  const char* id;
  const char* interface;
  // To Android's units: m/s² from milli-g, rad/s from milli-degrees per second.
  double scale;
};
constexpr SensorInfo SENSORS[PhoneSensors::SensorCount] = {
    {"accelerometersensor", "local.AccelerometerSensor", 9.80665 / 1000.0},
    {"gyroscopesensor", "local.GyroscopeSensor", std::numbers::pi / 180.0 / 1000.0},
};

// sensorfw's TimedXyzData: quint64 timestamp (µs), float x, y, z; 24 bytes with padding.
constexpr int SAMPLE_SIZE = 24;
// 100 Hz asked (the accelerometer gives it; this phone's gyroscope stays near 20 Hz, stage 6.6).
constexpr int INTERVAL_MS = 10;
// Started on a read, stopped this long after the last one.
constexpr qint64 IDLE_STOP_MS = 5000;

qint64 NowMs()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

struct PhoneSensors::Session
{
  int id = -1;
  QLocalSocket* socket = nullptr;
  QByteArray buffer;
  bool tag_read = false;
};

PhoneSensors::PhoneSensors(QObject* parent) : QObject(parent)
{
  s_instance = this;
  m_check_timer.setInterval(500);
  connect(&m_check_timer, &QTimer::timeout, this, &PhoneSensors::Check);
  m_check_timer.start();
}

PhoneSensors::~PhoneSensors()
{
  for (int s = 0; s < SensorCount; ++s)
    Stop(static_cast<Sensor>(s));
  s_instance = nullptr;
}

PhoneSensors* PhoneSensors::Instance()
{
  return s_instance;
}

double PhoneSensors::Read(Sensor sensor, int axis)
{
  m_last_read_ms.store(NowMs(), std::memory_order_relaxed);
  return m_values[sensor][axis].load(std::memory_order_relaxed);
}

void PhoneSensors::Check()
{
  const qint64 idle = NowMs() - m_last_read_ms.load(std::memory_order_relaxed);
  // Test aid: DOLPHIN_UT_SENSOR_LOG=1 logs the screen-axis values once a second while running
  // (development builds only, stage 7).
#ifdef DOLPHIN_UT_DEV_TOOLS
  static const bool log_values = qEnvironmentVariableIsSet("DOLPHIN_UT_SENSOR_LOG");
#else
  constexpr bool log_values = false;
#endif
  static int ticks = 0;
  if (log_values && m_sessions[Accelerometer] && ++ticks % 2 == 0)
  {
    const auto value = [this](Sensor s, int a) { return m_values[s][a].load(); };
    Emulator::LogFromAnyThread(
        QStringLiteral("phone sensors: accel %1 %2 %3 m/s², gyro %4 %5 %6 rad/s, %7, side %8, "
                       "samples %9/%10")
            .arg(value(Accelerometer, 0), 0, 'f', 2)
            .arg(value(Accelerometer, 1), 0, 'f', 2)
            .arg(value(Accelerometer, 2), 0, 'f', 2)
            .arg(value(Gyroscope, 0), 0, 'f', 2)
            .arg(value(Gyroscope, 1), 0, 'f', 2)
            .arg(value(Gyroscope, 2), 0, 'f', 2)
            .arg(m_landscape ? QStringLiteral("landscape") : QStringLiteral("portrait"))
            .arg(m_side.load())
            .arg(m_samples[Accelerometer].load())
            .arg(m_samples[Gyroscope].load()));
  }
  for (int s = 0; s < SensorCount; ++s)
  {
    const auto sensor = static_cast<Sensor>(s);
    if (idle < 1000 && !m_sessions[s] && !m_unavailable)
    {
      if (!Start(sensor))
      {
        m_unavailable = true;
        Emulator::LogFromAnyThread(QStringLiteral("phone sensors: not available; motion from the "
                                                  "phone is off for this run"));
      }
    }
    else if (idle > IDLE_STOP_MS && m_sessions[s])
    {
      Stop(sensor);
    }
  }
}

bool PhoneSensors::Start(Sensor sensor)
{
  const SensorInfo& info = SENSORS[sensor];
  QDBusConnection bus = QDBusConnection::systemBus();
  QDBusInterface manager(QString::fromLatin1(SERVICE), QString::fromLatin1(MANAGER_PATH),
                         QString::fromLatin1(MANAGER_INTERFACE), bus);
  const QDBusReply<bool> loaded = manager.call(QStringLiteral("loadPlugin"),
                                               QString::fromLatin1(info.id));
  if (!loaded.isValid() || !loaded.value())
  {
    Emulator::LogFromAnyThread(QStringLiteral("phone sensors: loadPlugin %1 failed: %2")
                                   .arg(QString::fromLatin1(info.id), loaded.error().message()));
    return false;
  }
  const QDBusReply<int> requested =
      manager.call(QStringLiteral("requestSensor"), QString::fromLatin1(info.id),
                   static_cast<qint64>(QCoreApplication::applicationPid()));
  if (!requested.isValid() || requested.value() < 0)
  {
    Emulator::LogFromAnyThread(QStringLiteral("phone sensors: requestSensor %1 failed: %2")
                                   .arg(QString::fromLatin1(info.id), requested.error().message()));
    return false;
  }

  auto session = std::make_unique<Session>();
  session->id = requested.value();
  // The data socket must connect soon after the request, or sensorfwd drops the session.
  session->socket = new QLocalSocket(this);
  session->socket->connectToServer(QString::fromLatin1(SOCKET_PATH), QIODevice::ReadWrite);
  if (!session->socket->waitForConnected(1000))
  {
    Emulator::LogFromAnyThread(QStringLiteral("phone sensors: %1: %2")
                                   .arg(QString::fromLatin1(SOCKET_PATH),
                                        session->socket->errorString()));
    manager.call(QStringLiteral("releaseSensor"), QString::fromLatin1(info.id), session->id,
                 static_cast<qint64>(QCoreApplication::applicationPid()));
    delete session->socket;
    return false;
  }
  const qint32 id = session->id;
  session->socket->write(reinterpret_cast<const char*>(&id), sizeof(id));
  session->socket->flush();
  connect(session->socket, &QLocalSocket::readyRead, this, [this, sensor] { OnData(sensor); });

  QDBusInterface channel(QString::fromLatin1(SERVICE),
                         QStringLiteral("%1/%2").arg(QString::fromLatin1(MANAGER_PATH),
                                                     QString::fromLatin1(info.id)),
                         QString::fromLatin1(info.interface), bus);
  // Keep running with the display dimmed or off (a game keeps it on anyway).
  channel.call(QStringLiteral("setStandbyOverride"), id, true);
  channel.call(QStringLiteral("setInterval"), id, INTERVAL_MS);
  const QDBusMessage started = channel.call(QStringLiteral("start"), id);
  if (started.type() == QDBusMessage::ErrorMessage)
  {
    Emulator::LogFromAnyThread(QStringLiteral("phone sensors: start %1 failed: %2")
                                   .arg(QString::fromLatin1(info.id), started.errorMessage()));
  }
  m_samples[sensor] = 0;
  m_sessions[sensor] = std::move(session);
  Emulator::LogFromAnyThread(QStringLiteral("phone sensors: %1 started (session %2)")
                                 .arg(QString::fromLatin1(info.id))
                                 .arg(id));
  return true;
}

void PhoneSensors::Stop(Sensor sensor)
{
  std::unique_ptr<Session> session = std::move(m_sessions[sensor]);
  if (!session)
    return;
  const SensorInfo& info = SENSORS[sensor];
  QDBusConnection bus = QDBusConnection::systemBus();
  QDBusInterface channel(QString::fromLatin1(SERVICE),
                         QStringLiteral("%1/%2").arg(QString::fromLatin1(MANAGER_PATH),
                                                     QString::fromLatin1(info.id)),
                         QString::fromLatin1(info.interface), bus);
  channel.call(QStringLiteral("stop"), session->id);
  QDBusInterface manager(QString::fromLatin1(SERVICE), QString::fromLatin1(MANAGER_PATH),
                         QString::fromLatin1(MANAGER_INTERFACE), bus);
  manager.call(QStringLiteral("releaseSensor"), QString::fromLatin1(info.id), session->id,
               static_cast<qint64>(QCoreApplication::applicationPid()));
  session->socket->disconnect(this);
  session->socket->disconnectFromServer();
  session->socket->deleteLater();
  for (auto& value : m_values[sensor])
    value = 0.0;
  Emulator::LogFromAnyThread(QStringLiteral("phone sensors: %1 stopped after %2 samples")
                                 .arg(QString::fromLatin1(info.id))
                                 .arg(m_samples[sensor].load()));
}

void PhoneSensors::OnData(Sensor sensor)
{
  Session* session = m_sessions[sensor].get();
  if (!session)
    return;
  session->buffer += session->socket->readAll();
  QByteArray& buffer = session->buffer;
  qsizetype pos = 0;
  if (!session->tag_read && !buffer.isEmpty())
  {
    // sensorfwd's tag byte ("\n") on a new connection.
    session->tag_read = true;
    pos = 1;
  }
  while (buffer.size() - pos >= 4)
  {
    quint32 count;
    std::memcpy(&count, buffer.constData() + pos, sizeof(count));
    if (count == 0 || count > 1000)
    {
      // Out of step: start over with the next batch (libsensorclient flushes too).
      buffer.clear();
      return;
    }
    const qsizetype size = 4 + qsizetype(count) * SAMPLE_SIZE;
    if (buffer.size() - pos < size)
      break;
    // The newest sample of the batch.
    const char* last = buffer.constData() + pos + 4 + qsizetype(count - 1) * SAMPLE_SIZE;
    float xyz[3];
    std::memcpy(xyz, last + 8, sizeof(xyz));
    Store(sensor, xyz[0], xyz[1], xyz[2]);
    m_samples[sensor] += int(count);
    pos += size;
  }
  buffer.remove(0, pos);
}

void PhoneSensors::Store(Sensor sensor, float raw_x, float raw_y, float raw_z)
{
  const double scale = SENSORS[sensor].scale;
  const double x = raw_x * scale;
  const double y = raw_y * scale;
  const double z = raw_z * scale;
  // Which landscape: the top edge is where gravity is not. Held with the top edge to the left,
  // the device's x axis points up and reads +1 g (Android's ROTATION_90). Only when clearly so,
  // as a shell rotates; flat or upright keeps the last side.
  if (sensor == Accelerometer && std::abs(x) > 5.0 && std::abs(x) > std::abs(y))
    m_side = x > 0 ? 1 : -1;
  // DolphinSensorEventListener.onSensorChanged's rotation into screen coordinates.
  double screen_x = x;
  double screen_y = y;
  if (m_landscape)
  {
    if (m_side > 0)
    {
      screen_x = -y;  // ROTATION_90
      screen_y = x;
    }
    else
    {
      screen_x = y;  // ROTATION_270
      screen_y = -x;
    }
  }
  m_values[sensor][0].store(screen_x, std::memory_order_relaxed);
  m_values[sensor][1].store(screen_y, std::memory_order_relaxed);
  m_values[sensor][2].store(z, std::memory_order_relaxed);
}

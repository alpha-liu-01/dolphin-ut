// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// dolphin-ut: the Ubuntu Touch frontend (docs/ut-port/stage3-roadmap.md, phase 3.0 shell).

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QImage>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWindow>

#include "Common/FileUtil.h"
#include "Common/MsgHandler.h"
#include "Common/StringUtil.h"
#include "DolphinUT/Controllers.h"
#include "DolphinUT/Emulator.h"
#include "DolphinUT/GameList.h"
#include "DolphinUT/GpuDrivers.h"
#include "DolphinUT/SettingsModel.h"
#include "DolphinUT/PortDefaults.h"
#include "DolphinUT/Storage.h"
#include "DolphinUT/WiiSystem.h"
#include "DolphinUT/Cheats.h"
#include "DolphinUT/DiscConverter.h"
#include "DolphinUT/Riivolution.h"
#include "DolphinUT/UserData.h"
#include "DolphinUT/PhoneSensors.h"
#include "DolphinUT/Achievements.h"
#include "Core/AchievementManager.h"
#include "DolphinUT/LomiriIntegration.h"
#include <OptionParser.h>

#include "Core/Core.h"
#include "Core/System.h"
#include "UICommon/CommandLineParse.h"
#include "VideoCommon/AsyncRequests.h"
#include "UICommon/UICommon.h"

namespace
{
// Lomiri gives apps QT_QPA_PLATFORM=ubuntumirclient;wayland-egl;xcb, and Qt 6 on Ubuntu Touch has
// that plugin, but Dolphin has no window system for it (docs/ut-port/stage2-wayland.md).
void DropUbuntuMirClientPlatform()
{
  const char* qt_platforms = std::getenv("QT_QPA_PLATFORM");
  if (!qt_platforms)
    return;

  std::string kept;
  for (const std::string& platform : SplitString(qt_platforms, ';'))
  {
    if (platform.empty() || Common::CaseInsensitiveContains(platform, "ubuntumirclient"))
      continue;
    if (!kept.empty())
      kept += ';';
    kept += platform;
  }
  if (kept.empty())
    unsetenv("QT_QPA_PLATFORM");
  else
    setenv("QT_QPA_PLATFORM", kept.c_str(), 1);
}

// libhybris' EGL offers only OpenGL ES configs. Qt's default (desktop OpenGL) config choice fails
// on it ("Cannot find EGLConfig", docs/ut-port/stage1-build.md), so ask for OpenGL ES up front.
void UseOpenGLES()
{
  QSurfaceFormat format = QSurfaceFormat::defaultFormat();
  format.setRenderableType(QSurfaceFormat::OpenGLES);
  format.setVersion(3, 0);
  QSurfaceFormat::setDefaultFormat(format);
}

std::string ArgumentValue(int argc, char** argv, const char* short_name, const char* long_name)
{
  for (int i = 1; i + 1 < argc; ++i)
  {
    const std::string arg = argv[i];
    if ((short_name && arg == short_name) || arg == long_name)
      return argv[i + 1];
  }
  return {};
}

// Dolphin's "-C <System>.<Section>.<Key>=<Value>" overrides (as in dolphin-emu-nogui), for tests
// such as single-core vs dual-core. Only those are handed to Dolphin's parser, which would reject
// this app's own options.
void AddCommandLineConfigLayer(int argc, char** argv)
{
  std::vector<std::string> arguments{argv[0]};
  for (int i = 1; i + 1 < argc; ++i)
  {
    const std::string arg = argv[i];
    if (arg == "-C" || arg == "--config")
    {
      arguments.push_back("-C");
      arguments.push_back(argv[++i]);
    }
  }
  static auto parser = CommandLineParse::CreateParser(CommandLineParse::ParserOptions::OmitGUIOptions);
  CommandLineParse::ParseArguments(parser.get(), arguments);
}

// Dolphin's panic alerts and questions ("An error occurred … Ignore and continue?"). Without a
// handler, Dolphin answers "no" on Linux, and an assertion then ends the app with SIGTRAP (S3.6).
// As DolphinQt and the Android app: a dialog (Yes/No, or OK; warnings also offer "Ignore for this
// session"), over the game if one runs, while the asking thread waits (stage 6.1). Repeats after a
// lost GPU are dropped first (stage 4).
bool OnMsgAlert(const char* caption, const char* text, bool yes_no, Common::MsgType style)
{
  if (Emulator::NoteAlert(text))
    return true;
  Emulator::LogFromAnyThread(QStringLiteral("alert (%1): %2 — %3")
                                 .arg(QString::fromUtf8(caption), QString::fromUtf8(text),
                                      yes_no ? QStringLiteral("asking") : QStringLiteral("shown")));
  return Emulator::AskFromAnyThread(caption, text, yes_no, style);
}

std::atomic<bool> s_quit_requested = false;

void OnQuitSignal(int)
{
  s_quit_requested = true;
}
}  // namespace

int main(int argc, char** argv)
{
  // SDL (the controller backend) would install its own SIGINT/SIGTERM handlers when controllers are
  // initialized, replacing ours and turning the signals into a game stop instead of an app quit.
  // Lomiri closes apps with SIGTERM, so the app must handle it itself.
  setenv("SDL_NO_SIGNAL_HANDLERS", "1", 0);
  DropUbuntuMirClientPlatform();
  // No QT_SCALE_FACTOR: Lomiri Components scale by GRID_UNIT_PX themselves (units.gu), and a Qt
  // scale factor on top of that scales everything twice (docs/ut-port/stage3.1-library.md).
  UseOpenGLES();

  // Started by Lomiri (APP_ID set): Qt's settings and caches, and Dolphin's user folder, go to the
  // app's own folders, which a confined click may write (stage 3.7).
  Storage::ConfigureApplication();
  UICommon::SetUserDirectory(Storage::UserFolder(ArgumentValue(argc, argv, "-u", "--user")));

  // Stage 4: the chosen Vulkan driver (Settings → Graphics → GPU Driver), set up before libhybris
  // starts with Qt's EGL (GpuDrivers.h, VulkanDriver.h).
  if (const std::string driver_note = GpuDrivers::ApplyAtStart(); !driver_note.empty())
  {
    std::printf("[dolphin-ut] %s\n", driver_note.c_str());
    std::fflush(stdout);
  }

  QGuiApplication app(argc, argv);

  Common::RegisterMsgAlertHandler(OnMsgAlert);
  AddCommandLineConfigLayer(argc, argv);
  UICommon::CreateDirectories();
  UICommon::Init();

  LomiriIntegration lomiri;
  Emulator emulator(&lomiri);

  // The port's defaults for this phone (stage 3.8): once per user folder, before anything reads them.
  for (const QString& line : PortDefaults::Apply())
    emulator.appendLog(QStringLiteral("default set: ") + line);

  // Shared folders, the screenshot folder, and the games folder in the library (before its scan).
  Storage::SetUp();
  Storage storage;
  GameListModel game_list;
  Controllers controllers;
  SettingsModel settings_model;
  GpuDrivers gpu_drivers;
  WiiSystem wii_system;
  Cheats cheats;
  Riivolution riivolution;
  DiscConverter disc_converter;
  UserData user_data;
  // Stage 6.6: the phone's accelerometer and gyroscope for the phone input device (sensorfwd).
  PhoneSensors phone_sensors;
  // Stage 6.7: RetroAchievements (starts Dolphin's AchievementManager if enabled).
  Achievements achievements;
  QObject::connect(&settings_model, &SettingsModel::valueChanged, &achievements,
                   [&achievements](const QString& name, const QVariant& value) {
                     if (name == QLatin1String("BooleanSetting.ACHIEVEMENTS_ENABLED"))
                       achievements.SetEnabled(value.toBool());
                   });
  QObject::connect(&controllers, &Controllers::message, &emulator, &Emulator::appendLog);
  controllers.SetVibrator([&lomiri](int duration_ms) { lomiri.vibrate(duration_ms); });
  qmlRegisterType<GameListFilter>("DolphinUT.Native", 1, 0, "GameListFilter");

  QQmlApplicationEngine engine;
  engine.addImageProvider(QStringLiteral("dolphin"), new GameImageProvider(&game_list));
  engine.rootContext()->setContextProperty(QStringLiteral("gameList"), &game_list);
  engine.rootContext()->setContextProperty(QStringLiteral("emulator"), &emulator);
  engine.rootContext()->setContextProperty(QStringLiteral("controllers"), &controllers);
  engine.rootContext()->setContextProperty(QStringLiteral("settingsModel"), &settings_model);
  engine.rootContext()->setContextProperty(QStringLiteral("lomiri"), &lomiri);
  engine.rootContext()->setContextProperty(QStringLiteral("storage"), &storage);
  engine.rootContext()->setContextProperty(QStringLiteral("gpuDrivers"), &gpu_drivers);
  engine.rootContext()->setContextProperty(QStringLiteral("wiiSystem"), &wii_system);
  engine.rootContext()->setContextProperty(QStringLiteral("cheats"), &cheats);
  engine.rootContext()->setContextProperty(QStringLiteral("riivolution"), &riivolution);
  engine.rootContext()->setContextProperty(QStringLiteral("discConverter"), &disc_converter);
  engine.rootContext()->setContextProperty(QStringLiteral("userData"), &user_data);
  engine.rootContext()->setContextProperty(QStringLiteral("achievements"), &achievements);
  // Development: --qml FILE loads that file instead of the app's UI (the phone has no Qt 6 qml
  // runtime), with the same context objects.
  const std::string qml_file = ArgumentValue(argc, argv, nullptr, "--qml");
  if (qml_file.empty())
    engine.loadFromModule("DolphinUT", "Main");
  else
    engine.load(QUrl::fromLocalFile(QString::fromStdString(qml_file)));
  if (engine.rootObjects().isEmpty())
    return 1;

  auto* launcher = qobject_cast<QWindow*>(engine.rootObjects().first());
  emulator.SetLauncherWindow(launcher);
  // The controllers exist now (UICommon::InitControllers in SetLauncherWindow).
  controllers.Start();

  // Development: --grab PNG [--grab-after MS] saves a screenshot of the app's window (default after
  // 4 s), so the UI can be checked over adb.
  const std::string grab_path = ArgumentValue(argc, argv, nullptr, "--grab");
  if (!grab_path.empty())
  {
    const std::string grab_after = ArgumentValue(argc, argv, nullptr, "--grab-after");
    QTimer::singleShot(grab_after.empty() ? 4000 : std::atoi(grab_after.c_str()), &app,
                       [launcher, grab_path] {
                         auto* window = qobject_cast<QQuickWindow*>(launcher);
                         const bool saved =
                             window && window->grabWindow().save(QString::fromStdString(grab_path));
                         std::printf("[dolphin-ut] grab %s: %s\n", grab_path.c_str(),
                                     saved ? "saved" : "failed");
                         std::fflush(stdout);
                       });
  }

  // For tests over adb: --start PATH [--backend Vulkan|OGL] [--state-slot N] boots a game without a
  // tap (with the configured backend unless --backend is given), and optionally loads a save state
  // (made with the overlay's Save) once it runs.
  const std::string state_slot = ArgumentValue(argc, argv, nullptr, "--state-slot");
  if (!state_slot.empty())
    emulator.SetBootStateSlot(std::atoi(state_slot.c_str()));
  // Development: --pause-after MS pauses the game that long after start (to check what a paused game
  // shows, such as the in-game menu, over adb).
  const std::string pause_after = ArgumentValue(argc, argv, nullptr, "--pause-after");
  if (!pause_after.empty())
    QTimer::singleShot(std::atoi(pause_after.c_str()), &emulator, [&emulator] { emulator.togglePause(); });
  const std::string start_path = ArgumentValue(argc, argv, nullptr, "--start");
  if (!start_path.empty())
  {
    const std::string backend = ArgumentValue(argc, argv, nullptr, "--backend");
    // --riivolution: with the choices saved on the Riivolution page (stage 6.4).
    bool with_riivolution = false;
    for (int i = 1; i < argc; ++i)
      with_riivolution |= std::string_view(argv[i]) == "--riivolution";
    QTimer::singleShot(1500, &emulator, [&emulator, start_path, backend, with_riivolution] {
      if (with_riivolution)
        emulator.startWithRiivolution(QString::fromStdString(start_path));
      else
        emulator.start(QString::fromStdString(start_path), QString::fromStdString(backend));
    });
    // --stop-after MS: exit the game back to the library, as the in-game menu's Exit does.
    const std::string stop_after = ArgumentValue(argc, argv, nullptr, "--stop-after");
    if (!stop_after.empty())
      QTimer::singleShot(1500 + std::atoi(stop_after.c_str()), &emulator, [&emulator] { emulator.stop(); });
  }

  // Stage 6.1 test aids over adb. --settings-after MS / --return-after MS: open the in-game menu's
  // Settings over the game, and return to it. --test-alert KIND@MS: one of Dolphin's alerts from a
  // worker thread, as the core's threads raise them (KIND: question, warning, info).
  // --answer-after MS:BUTTON: answer the newest dialog (0 OK/No, 1 Yes, 2 Ignore for this session).
  if (const std::string after = ArgumentValue(argc, argv, nullptr, "--settings-after"); !after.empty())
    QTimer::singleShot(std::atoi(after.c_str()), &emulator, [&emulator] { emulator.openSettingsOverGame(); });
  if (const std::string after = ArgumentValue(argc, argv, nullptr, "--return-after"); !after.empty())
    QTimer::singleShot(std::atoi(after.c_str()), &emulator, [&emulator] { emulator.returnToGame(); });
  if (const std::string alert = ArgumentValue(argc, argv, nullptr, "--test-alert"); !alert.empty())
  {
    const std::string kind = alert.substr(0, alert.find('@'));
    const int delay = std::atoi(alert.substr(alert.find('@') + 1).c_str());
    // cpu-question / gpu-warning: from a job on the CPU thread, or an event on the GPU thread
    // (dual core), the threads Dolphin's own alerts come from during a game.
    if (kind == "cpu-question" || kind == "gpu-warning")
    {
      QTimer::singleShot(delay, &emulator, [kind] {
        Core::RunOnCPUThread(Core::System::GetInstance(), [kind] {
          if (kind == "cpu-question")
          {
            const bool answer = AskYesNoFmt("Test question from the CPU thread. Continue?");
            Emulator::LogFromAnyThread(QStringLiteral("test alert cpu-question returned %1")
                                           .arg(answer ? "true" : "false"));
            return;
          }
          AsyncRequests::GetInstance()->PushEvent([] {
            const bool answer = PanicAlertFmt("Test warning from the GPU thread.");
            Emulator::LogFromAnyThread(QStringLiteral("test alert gpu-warning returned %1")
                                           .arg(answer ? "true" : "false"));
          });
        });
      });
    }
    else
    std::thread([kind, delay] {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
      bool answer = false;
      if (kind == "question")
        answer = AskYesNoFmt("Test question from a worker thread. Continue?");
      else if (kind == "warning")
        answer = PanicAlertFmt("Test warning from a worker thread.");
      else
        answer = SuccessAlertFmt("Test information from a worker thread.");
      Emulator::LogFromAnyThread(QStringLiteral("test alert %1 returned %2")
                                     .arg(QString::fromStdString(kind))
                                     .arg(answer ? "true" : "false"));
    }).detach();
  }
  if (const std::string answer = ArgumentValue(argc, argv, nullptr, "--answer-after"); !answer.empty())
  {
    const int delay = std::atoi(answer.substr(0, answer.find(':')).c_str());
    const int button = std::atoi(answer.substr(answer.find(':') + 1).c_str());
    QTimer::singleShot(delay, &emulator, [&emulator, button] {
      emulator.answerQuestion(emulator.LatestQuestionId(), button);
    });
  }
  // Stage 6.2 test aid: --test-action ACTION@MS runs a settings menu action (SettingsModel::
  // runAction, e.g. jit_write_block_log) during a game; the result goes to the log. Repeatable.
  for (int i = 1; i + 1 < argc; ++i)
  {
    if (std::string_view(argv[i]) != "--test-action")
      continue;
    const std::string spec = argv[i + 1];
    const QString action = QString::fromStdString(spec.substr(0, spec.find('@')));
    const int delay = std::atoi(spec.substr(spec.find('@') + 1).c_str());
    QTimer::singleShot(delay, &settings_model,
                       [&settings_model, action] { settings_model.runAction(action); });
  }

  // SIGINT/SIGTERM (adb tests, the session ending): stop the game and quit cleanly.
  std::signal(SIGINT, OnQuitSignal);
  std::signal(SIGTERM, OnQuitSignal);
  QTimer quit_poll;
  QObject::connect(&quit_poll, &QTimer::timeout, &app, [] {
    // exit(), not quit(): Qt 6's quit() first asks the windows to close, and the game window turns
    // a close into "stop the game" and stays open, which cancels the quit. Emulator::Shutdown()
    // stops the game after exec() returns.
    if (s_quit_requested.exchange(false))
      QCoreApplication::exit(0);
  });
  quit_poll.start(100);

  const int result = app.exec();
  emulator.Shutdown();
  // Before the config goes (the Achievements object outlives this function's end).
  AchievementManager::GetInstance().Shutdown();
  UICommon::Shutdown();
  return result;
}

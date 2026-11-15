// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// dolphin-ut: the Ubuntu Touch frontend (docs/ut-port/stage3-roadmap.md, phase 3.0 shell).

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <string>
#include <vector>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWindow>

#include "Common/StringUtil.h"
#include "DolphinUT/Emulator.h"
#include "DolphinUT/LomiriIntegration.h"
#include <OptionParser.h>

#include "UICommon/CommandLineParse.h"
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

// Lomiri scales apps by GRID_UNIT_PX (1 grid unit = 8 dp; 21 on the Redmi Note 12 5G), while the
// Wayland output reports a scale of 1. Qt Quick Controls size things in dp, so turn the grid unit
// into Qt's scale factor unless the user set one.
void ApplyLomiriScale()
{
  if (std::getenv("QT_SCALE_FACTOR"))
    return;
  const char* grid_unit = std::getenv("GRID_UNIT_PX");
  if (!grid_unit)
    return;
  const double scale = std::atof(grid_unit) / 8.0;
  if (scale > 0.5 && scale < 8.0)
    setenv("QT_SCALE_FACTOR", std::to_string(scale).c_str(), 1);
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
  ApplyLomiriScale();
  UseOpenGLES();

  QCoreApplication::setOrganizationName(QStringLiteral("Dolphin Emulator"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("dolphin-emu.org"));
  QCoreApplication::setApplicationName(QStringLiteral("dolphin-ut"));
  QGuiApplication app(argc, argv);
  QQuickStyle::setStyle(QStringLiteral("Material"));

  AddCommandLineConfigLayer(argc, argv);
  UICommon::SetUserDirectory(ArgumentValue(argc, argv, "-u", "--user"));
  UICommon::CreateDirectories();
  UICommon::Init();

  LomiriIntegration lomiri;
  Emulator emulator(&lomiri);

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("emulator"), &emulator);
  engine.rootContext()->setContextProperty(QStringLiteral("lomiri"), &lomiri);
  engine.loadFromModule("DolphinUT", "Main");
  if (engine.rootObjects().isEmpty())
    return 1;

  auto* launcher = qobject_cast<QWindow*>(engine.rootObjects().first());
  emulator.SetLauncherWindow(launcher);

  // For tests over adb: --start PATH [--backend Vulkan|OGL] [--state-slot N] boots a game without a
  // tap, and optionally loads a save state (made with the overlay's Save) once it runs.
  const std::string state_slot = ArgumentValue(argc, argv, nullptr, "--state-slot");
  if (!state_slot.empty())
    emulator.SetBootStateSlot(std::atoi(state_slot.c_str()));
  const std::string start_path = ArgumentValue(argc, argv, nullptr, "--start");
  if (!start_path.empty())
  {
    const std::string backend = ArgumentValue(argc, argv, nullptr, "--backend");
    QTimer::singleShot(1500, &emulator, [&emulator, start_path, backend] {
      emulator.start(QString::fromStdString(start_path),
                     QString::fromStdString(backend.empty() ? "Vulkan" : backend));
    });
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
  UICommon::Shutdown();
  return result;
}

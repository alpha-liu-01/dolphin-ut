// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/Emulator.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformwindow.h>

#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/State.h"
#include "Core/System.h"
#include "DiscIO/Enums.h"
#include "DolphinUT/GameWindow.h"
#include "DolphinUT/LomiriIntegration.h"
#include "DolphinUT/TouchOverlay.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "UICommon/GameFile.h"
#include "UICommon/UICommon.h"
#include "VideoCommon/PerformanceMetrics.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/VideoBackendBase.h"

namespace
{
std::atomic<Emulator*> s_instance = nullptr;
std::atomic<bool> s_game_window_active = false;

QString StateName(Core::State state)
{
  switch (state)
  {
  case Core::State::Uninitialized:
    return QStringLiteral("idle");
  case Core::State::Paused:
    return QStringLiteral("paused");
  case Core::State::Running:
    return QStringLiteral("running");
  case Core::State::Stopping:
    return QStringLiteral("stopping");
  case Core::State::Starting:
    return QStringLiteral("starting");
  }
  return QStringLiteral("?");
}

QString AppStateName(Qt::ApplicationState state)
{
  switch (state)
  {
  case Qt::ApplicationSuspended:
    return QStringLiteral("suspended");
  case Qt::ApplicationHidden:
    return QStringLiteral("hidden");
  case Qt::ApplicationInactive:
    return QStringLiteral("inactive");
  case Qt::ApplicationActive:
    return QStringLiteral("active");
  }
  return QStringLiteral("?");
}
}  // namespace

Emulator::Emulator(LomiriIntegration* lomiri, QObject* parent) : QObject(parent), m_lomiri(lomiri)
{
  s_instance = this;
  connect(m_lomiri, &LomiriIntegration::message, this, &Emulator::appendLog);

  // Jobs the core queues for the host thread (stop completion and others).
  m_host_jobs.setInterval(10);
  connect(&m_host_jobs, &QTimer::timeout, this,
          [] { Core::HostDispatchJobs(Core::System::GetInstance()); });
  m_host_jobs.start();

  // Speed while a game runs, for comparing settings (single vs dual core, backends) without the OSD.
  m_perf_log.setInterval(5000);
  connect(&m_perf_log, &QTimer::timeout, this, [this] {
    auto& system = Core::System::GetInstance();
    if (Core::GetState(system) != Core::State::Running)
      return;
    const PerformanceMetrics& perf = system.GetPerfMetrics();
    appendLog(QStringLiteral("perf: %1 fps, %2 vps, %3% speed (%4)")
                  .arg(perf.GetFPS(), 0, 'f', 1)
                  .arg(perf.GetVPS(), 0, 'f', 1)
                  .arg(perf.GetSpeed() * 100.0, 0, 'f', 0)
                  .arg(system.IsDualCoreMode() ? QStringLiteral("dual core") :
                                                 QStringLiteral("single core")));
  });
  m_perf_log.start();

  m_state_hook = Core::AddOnStateChangedCallback([this](Core::State state) {
    QMetaObject::invokeMethod(this, [this, state] { OnCoreStateChanged(state); },
                              Qt::QueuedConnection);
  });

  connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
          &Emulator::OnApplicationStateChanged);

  appendLog(QStringLiteral("Qt platform '%1', scale %2")
                .arg(QGuiApplication::platformName())
                .arg(qgetenv("QT_SCALE_FACTOR").isEmpty() ? QStringLiteral("1") :
                                                            QString::fromUtf8(qgetenv("QT_SCALE_FACTOR"))));
}

Emulator::~Emulator()
{
  s_instance = nullptr;
}

void Emulator::SetLauncherWindow(QWindow* window)
{
  m_launcher = window;
  if (!window || m_controllers_initialized)
    return;
  window->create();
  UICommon::InitControllers(MakeWindowSystemInfo(window));
  m_controllers_initialized = true;
}

void Emulator::Shutdown()
{
  auto& system = Core::System::GetInstance();
  if (Core::GetState(system) != Core::State::Uninitialized)
  {
    Core::Stop(system);
    while (Core::GetState(system) != Core::State::Uninitialized)
    {
      Core::HostDispatchJobs(system);
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
  }
  FinishStop();
  Core::Shutdown(system);
  if (m_controllers_initialized)
    UICommon::ShutdownControllers();
  m_controllers_initialized = false;
}

WindowSystemInfo Emulator::MakeWindowSystemInfo(QWindow* window) const
{
  WindowSystemInfo wsi;
  const QString platform = QGuiApplication::platformName();
  QPlatformNativeInterface* native = QGuiApplication::platformNativeInterface();
  if (platform.startsWith(QStringLiteral("wayland")))
  {
    wsi.type = WindowSystemType::Wayland;
    wsi.display_connection = native->nativeResourceForWindow("display", window);
    wsi.render_window = native->nativeResourceForWindow("surface", window);
  }
  else if (platform == QStringLiteral("xcb"))
  {
    wsi.type = WindowSystemType::X11;
    wsi.display_connection = native->nativeResourceForWindow("display", window);
    wsi.render_window = reinterpret_cast<void*>(window->winId());
  }
  wsi.render_surface = wsi.render_window;
  // Dolphin sizes its on-screen UI by this. Qt's device pixel ratio is 1 under Lomiri Components
  // (3.1), so use Lomiri's grid-unit scale, as native apps do.
  wsi.render_surface_scale = std::max(static_cast<float>(window->devicePixelRatio()),
                                      LomiriIntegration::GridUnitScale());
  const QSize physical = window->handle() ? window->handle()->geometry().size() :
                                            window->size() * window->devicePixelRatio();
  wsi.render_surface_width = physical.width();
  wsi.render_surface_height = physical.height();
  return wsi;
}

void Emulator::start(const QString& path, const QString& backend)
{
  auto& system = Core::System::GetInstance();
  if (Core::GetState(system) != Core::State::Uninitialized || m_game_window)
  {
    appendLog(QStringLiteral("start ignored: a game is already running"));
    return;
  }

  // An explicit backend applies to this run only (the current-run layer is never saved). Without
  // one, the configured backend is used, including per-game settings: the core activates it at
  // boot (Core::Init → VideoBackendBase::PopulateBackendInfo).
  if (backend.isEmpty())
    Config::DeleteKey(Config::LayerType::CurrentRun, Config::MAIN_GFX_BACKEND);
  else
    Config::SetCurrent(Config::MAIN_GFX_BACKEND, backend.toStdString());

  const UICommon::GameFile game(path.toStdString());
  const bool gamecube = game.GetPlatform() == DiscIO::Platform::GameCubeDisc ||
                        game.GetPlatform() == DiscIO::Platform::Triforce;
  appendLog(QStringLiteral("start %1 (%2, %3)")
                .arg(QFileInfo(path).fileName(), gamecube ? QStringLiteral("GameCube") :
                                                            QStringLiteral("Wii"),
                     backend.isEmpty() ?
                         QStringLiteral("configured: %1")
                             .arg(QString::fromStdString(Config::Get(Config::MAIN_GFX_BACKEND))) :
                         backend));

  m_game_window = new GameWindow();
  connect(m_game_window, &GameWindow::SizeChanged, this, &Emulator::OnGameWindowSizeChanged);
  connect(m_game_window, &GameWindow::CloseRequested, this, &Emulator::stop);
  connect(m_game_window, &GameWindow::StopRequested, this, &Emulator::stop);
  // Until the in-game menu (3.4) exists, Back pauses or resumes.
  connect(m_game_window, &GameWindow::MenuRequested, this, [this] {
    appendLog(QStringLiteral("menu requested (no in-game menu yet): pause/resume"));
    togglePause();
  });
  connect(m_game_window, &QWindow::activeChanged, this,
          [this] { s_game_window_active = m_game_window && m_game_window->isActive(); });
  m_lomiri->PreferLandscape(m_game_window);
  // Lomiri makes the game window fullscreen (top panel hidden) only if it asks once mapped (see
  // GameWindow::exposeEvent) and no other window of the app is showing: hide the library first.
  if (m_hide_launcher && m_launcher)
    m_launcher->hide();
  m_game_window->show();

  TouchOverlay::Actions actions;
  actions.exit = [] { RequestStopFromAnyThread(); };
  actions.save_state = [this] {
    appendLog(QStringLiteral("save state slot %1").arg(TEST_STATE_SLOT));
    State::Save(Core::System::GetInstance(), TEST_STATE_SLOT);
  };
  actions.load_state = [this] {
    appendLog(QStringLiteral("load state slot %1").arg(TEST_STATE_SLOT));
    State::Load(Core::System::GetInstance(), TEST_STATE_SLOT);
  };
  // Controls are sized by the screen's shorter side, as in the Android app.
  const QSize screen = m_game_window->screen()->size() * m_game_window->devicePixelRatio();
  m_overlay = std::make_unique<TouchOverlay>(!gamecube, std::min(screen.width(), screen.height()),
                                             std::move(actions));
  static constexpr const char* type_names[] = {"GameCube", "Wii Remote", "Wii Remote sideways",
                                               "Wii Remote + Nunchuk", "Classic Controller",
                                               "none"};
  appendLog(QStringLiteral("overlay: %1, controller %2")
                .arg(QString::fromLatin1(type_names[static_cast<int>(m_overlay->GetType())]))
                .arg(m_overlay->GetControllerIndex() + 1));
  m_game_window->SetOverlay(m_overlay.get());

  const WindowSystemInfo wsi = MakeWindowSystemInfo(m_game_window);
  appendLog(QStringLiteral("game window %1x%2, wl_surface %3")
                .arg(wsi.render_surface_width)
                .arg(wsi.render_surface_height)
                .arg(reinterpret_cast<quintptr>(wsi.render_surface), 0, 16));
  g_controller_interface.ChangeWindow(wsi.render_window);
  m_lomiri->SetKeepDisplayOn(true);

  if (!BootManager::BootCore(system, BootParameters::GenerateFromFile(path.toStdString()), wsi))
  {
    appendLog(QStringLiteral("BootCore failed"));
    FinishStop();
  }
}

void Emulator::stop()
{
  auto& system = Core::System::GetInstance();
  if (Core::GetState(system) == Core::State::Uninitialized)
  {
    FinishStop();
    return;
  }
  if (Core::GetState(system) == Core::State::Stopping)
    return;
  appendLog(QStringLiteral("stop requested"));
  Core::Stop(system);
}

void Emulator::FinishStop()
{
  m_lomiri->SetKeepDisplayOn(false);
  if (m_game_window)
    m_game_window->SetOverlay(nullptr);
  m_overlay.reset();
  if (m_game_window)
  {
    m_game_window->destroy();
    m_game_window->deleteLater();
    m_game_window = nullptr;
  }
  s_game_window_active = false;
  m_paused_by_app_state = false;
  if (m_launcher && !m_launcher->isVisible())
    m_launcher->show();
  if (m_launcher && m_controllers_initialized)
    g_controller_interface.ChangeWindow(MakeWindowSystemInfo(m_launcher).render_window);
}

void Emulator::OnCoreStateChanged(Core::State state)
{
  const Core::State previous = m_core_state;
  m_core_state = state;
  m_state = StateName(state);
  emit stateChanged();
  appendLog(QStringLiteral("core: %1").arg(m_state));

  // A wl_surface cannot report its size: send it again once the presenter exists
  // (the same fix as DolphinQt's RenderWidget, docs/ut-port/stage2-wayland.md).
  if (previous == Core::State::Starting &&
      (state == Core::State::Running || state == Core::State::Paused) && m_game_window)
  {
    const QSize physical = m_game_window->PhysicalSize();
    OnGameWindowSizeChanged(physical.width(), physical.height(),
                            static_cast<float>(m_game_window->devicePixelRatio()));

    // --state-slot: start every test run from the same point.
    if (m_boot_state_slot > 0)
    {
      appendLog(QStringLiteral("load state slot %1 (--state-slot)").arg(m_boot_state_slot));
      State::Load(Core::System::GetInstance(), static_cast<u32>(m_boot_state_slot));
    }
  }

  if (state == Core::State::Uninitialized)
    FinishStop();
}

void Emulator::OnApplicationStateChanged(Qt::ApplicationState state)
{
  appendLog(QStringLiteral("application state: %1").arg(AppStateName(state)));
  if (!m_pause_when_inactive)
    return;

  auto& system = Core::System::GetInstance();
  if (state != Qt::ApplicationActive && Core::GetState(system) == Core::State::Running)
  {
    Core::SetState(system, Core::State::Paused);
    m_paused_by_app_state = true;
    appendLog(QStringLiteral("paused (app not active)"));
  }
  else if (state == Qt::ApplicationActive && m_paused_by_app_state &&
           Core::GetState(system) == Core::State::Paused)
  {
    Core::SetState(system, Core::State::Running);
    m_paused_by_app_state = false;
    appendLog(QStringLiteral("resumed (app active)"));
  }
}

void Emulator::togglePause()
{
  auto& system = Core::System::GetInstance();
  const Core::State state = Core::GetState(system);
  if (state == Core::State::Running)
    Core::SetState(system, Core::State::Paused);
  else if (state == Core::State::Paused)
    Core::SetState(system, Core::State::Running);
  // A manual pause or resume overrides the automatic one for app switches.
  m_paused_by_app_state = false;
}

bool Emulator::GetShowPerformanceStats() const
{
  return Config::Get(Config::GFX_SHOW_FPS) && Config::Get(Config::GFX_SHOW_SPEED);
}

void Emulator::SetShowPerformanceStats(bool show)
{
  if (show == GetShowPerformanceStats())
    return;
  Config::SetBase(Config::GFX_SHOW_FPS, show);
  Config::SetBase(Config::GFX_SHOW_SPEED, show);
  Config::Save();
  emit settingsChanged();
}

void Emulator::OnGameWindowSizeChanged(int width, int height, float scale)
{
  appendLog(QStringLiteral("game window size %1x%2 (scale %3)").arg(width).arg(height).arg(scale));
  g_controller_interface.SetRenderSurfaceSize(width, height, scale);
  if (g_presenter)
  {
    g_presenter->ResizeSurface(width, height);
    // As the Android app's portrait layout (layout-port/fragment_emulation.xml): the picture in the
    // top half, the touch controls below.
    if (height > width)
      g_presenter->SetDrawArea(0.0f, 0.0f, 1.0f, 0.5f);
    else
      g_presenter->SetDrawArea(0.0f, 0.0f, 1.0f, 1.0f);
  }
}

void Emulator::appendLog(const QString& text)
{
  const QString line =
      QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz ")) + text;
  std::fprintf(stdout, "[dolphin-ut] %s\n", line.toUtf8().constData());
  std::fflush(stdout);
  m_log += line + QLatin1Char('\n');
  if (m_log.size() > 20000)
    m_log = m_log.right(15000);
  emit logChanged();
}

void Emulator::LogFromAnyThread(const QString& text)
{
  Emulator* instance = s_instance;
  if (instance)
    QMetaObject::invokeMethod(instance, [instance, text] { instance->appendLog(text); },
                              Qt::QueuedConnection);
}

void Emulator::RequestStopFromAnyThread()
{
  Emulator* instance = s_instance;
  if (instance)
    QMetaObject::invokeMethod(instance, [instance] { instance->stop(); }, Qt::QueuedConnection);
}

bool Emulator::GameWindowHasFocus()
{
  return s_game_window_active;
}

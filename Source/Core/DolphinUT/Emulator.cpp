// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/Emulator.h"

#include "DolphinUT/CpuBoost.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string_view>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformwindow.h>

#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/CommonTitles.h"
#include "DiscIO/RiivolutionParser.h"
#include "DiscIO/Volume.h"
#include "Common/FileUtil.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/HW/DVD/DVDInterface.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/HW/WiimoteReal/WiimoteReal.h"
#include "Core/State.h"
#include "Core/System.h"
#include "DiscIO/Enums.h"
#include "DolphinUT/GameWindow.h"
#include "DolphinUT/HotkeyScheduler.h"
#include "DolphinUT/PhoneSensors.h"
#include "DolphinUT/LomiriIntegration.h"
#include "DolphinUT/TouchOverlay.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "UICommon/GameFile.h"
#include "UICommon/UICommon.h"
#include "VideoCommon/AsyncRequests.h"
#include "VideoCommon/OnScreenDisplay.h"
#include "VideoCommon/PerformanceMetrics.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoEvents.h"

namespace
{
// Set by Emulator::NoteAlert (any thread) when the video backend failed to initialize.
std::atomic<bool> s_video_backend_failed = false;
// Set when Vulkan reports a lost device during play (stage 4: the driver hangs the GPU on Wind
// Waker); the repeats are counted, not shown.
std::atomic<bool> s_device_lost = false;
std::atomic<int> s_device_lost_repeats = 0;
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

QString WindowStateName(Qt::WindowStates state)
{
  if (state & Qt::WindowFullScreen)
    return QStringLiteral("fullscreen");
  if (state & Qt::WindowMaximized)
    return QStringLiteral("maximized");
  if (state & Qt::WindowMinimized)
    return QStringLiteral("minimized");
  return QStringLiteral("normal");
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
    // Threads started since (shader compilers, …) get the boost too.
    if (m_cpu_boost > 0)
      CpuBoost::Apply(m_cpu_boost);
#ifdef DOLPHIN_UT_DEV_TOOLS
    // Development builds only (stage 7): the speed every 5 s in the log.
    const PerformanceMetrics& perf = system.GetPerfMetrics();
    appendLog(QStringLiteral("perf: %1 fps, %2 vps, %3% speed (%4)")
                  .arg(perf.GetFPS(), 0, 'f', 1)
                  .arg(perf.GetVPS(), 0, 'f', 1)
                  .arg(perf.GetSpeed() * 100.0, 0, 'f', 0)
                  .arg(system.IsDualCoreMode() ? QStringLiteral("dual core") :
                                                 QStringLiteral("single core")));
#endif
  });
  m_perf_log.start();

  // A paused game presents nothing, so a menu drawn into the picture would freeze (and could not be
  // used to unpause). While paused with a menu open, ask the video thread to present the last frame
  // again, about 30 times a second: a job on the paused CPU thread queues it for the video thread
  // (AsyncRequests, which the GPU loop runs even while paused; in single core it runs on the CPU
  // thread, which then owns the video backend).
  m_paused_redraw.setInterval(33);
  connect(&m_paused_redraw, &QTimer::timeout, this, [this] {
    auto& system = Core::System::GetInstance();
    if (!m_overlay || Core::GetState(system) != Core::State::Paused ||
        !m_overlay->WantsPausedRedraw())
    {
      return;
    }
    Core::RunOnCPUThread(system, [] {
      AsyncRequests::GetInstance()->PushEvent([] {
        if (!g_presenter)
          return;
        PresentInfo info{};
        GetVideoEvents().before_present_event.Trigger(info);
        g_presenter->Present();
      });
    });
  });
  m_paused_redraw.start();

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
  // Lomiri decides the windows' states and sizes (stage 5: fullscreen carried over between them).
  connect(window, &QWindow::windowStateChanged, this, [this, window](Qt::WindowState state) {
    appendLog(QStringLiteral("library window: %1").arg(WindowStateName(state)));
    // Lomiri keeps one saved window state per app and gives it to the next window it maps: the
    // library, shown again after a game, came back fullscreen (S5). Ask for a normal window.
    if ((state & Qt::WindowFullScreen) && !m_game_window && window->isVisible() &&
        m_library_normal_requests < 3)
    {
      ++m_library_normal_requests;
      QTimer::singleShot(0, window, [this, window] {
        appendLog(QStringLiteral("library window: asking for a normal window"));
        window->showNormal();
      });
    }
  });
  const auto log_size = [this, window] {
    if (window->isVisible())
      appendLog(QStringLiteral("library window size %1x%2").arg(window->width()).arg(window->height()));
  };
  connect(window, &QWindow::widthChanged, this, log_size);
  connect(window, &QWindow::heightChanged, this, log_size);
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
  HotkeyScheduler::Stop();
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

void Emulator::Boot(const QString& path, const QString& backend, bool riivolution)
{
  auto& system = Core::System::GetInstance();
  if (Core::GetState(system) != Core::State::Uninitialized || m_game_window)
  {
    appendLog(QStringLiteral("start ignored: a game is already running"));
    return;
  }

  m_boot_path = path;
  m_boot_riivolution = riivolution;
  m_boot_reached_running = false;
  s_video_backend_failed = false;
  s_device_lost = false;
  s_device_lost_repeats = 0;

  // An explicit backend applies to this run only (the current-run layer is never saved). Without
  // one, the configured backend is used, including per-game settings: the core activates it at
  // boot (Core::Init → VideoBackendBase::PopulateBackendInfo).
  if (backend.isEmpty())
    Config::DeleteKey(Config::LayerType::CurrentRun, Config::MAIN_GFX_BACKEND);
  else
    Config::SetCurrent(Config::MAIN_GFX_BACKEND, backend.toStdString());

  const bool system_menu = path == QLatin1String(SYSTEM_MENU_PATH);
  const UICommon::GameFile game(system_menu ? std::string() : path.toStdString());
  const bool gamecube = !system_menu && (game.GetPlatform() == DiscIO::Platform::GameCubeDisc ||
                                         game.GetPlatform() == DiscIO::Platform::Triforce);
  appendLog(QStringLiteral("start %1 (%2, %3)")
                .arg(system_menu ? QStringLiteral("the Wii System Menu") : QFileInfo(path).fileName(),
                     gamecube ? QStringLiteral("GameCube") : QStringLiteral("Wii"),
                     backend.isEmpty() ?
                         QStringLiteral("configured: %1")
                             .arg(QString::fromStdString(Config::Get(Config::MAIN_GFX_BACKEND))) :
                         backend));

  m_game_window = new GameWindow();
  connect(m_game_window, &GameWindow::SizeChanged, this, &Emulator::OnGameWindowSizeChanged);
  connect(m_game_window, &GameWindow::CloseRequested, this, &Emulator::stop);
  connect(m_game_window, &GameWindow::StopRequested, this, &Emulator::stop);
  // Until the in-game menu (3.4) exists, Back pauses or resumes.
  // Back / Escape opens or closes the in-game menu, as in the Android app.
  connect(m_game_window, &GameWindow::MenuRequested, this, [this] {
    if (m_overlay)
      m_overlay->ToggleMenu();
  });
  connect(m_game_window, &QWindow::windowStateChanged, this, [this](Qt::WindowState state) {
    appendLog(QStringLiteral("game window: %1").arg(WindowStateName(state)));
  });
  connect(m_game_window, &QWindow::activeChanged, this,
          [this] { s_game_window_active = m_game_window && m_game_window->isActive(); });
  m_lomiri->PreferLandscape(m_game_window);
  // Lomiri makes the game window fullscreen (top panel hidden) only if it asks once mapped (see
  // GameWindow::exposeEvent) and no other window of the app is showing: hide the library first.
  if (m_hide_launcher && m_launcher)
    m_launcher->hide();
  m_game_window->show();

  // The in-game menu's actions (the Android app's EmulationActivity.handleMenuAction), all run on
  // the UI thread.
  TouchOverlay::Actions actions;
  actions.game_title = system_menu ?
                           std::string("Wii System Menu") :
                           game.GetName(UICommon::GameFile::Variant::LongAndPossiblyCustom);
  actions.exit = [] { RequestStopFromAnyThread(); };
  actions.toggle_pause = [this] { togglePause(); };
  actions.screenshot = [] { Core::SaveScreenShot(); };
  actions.save_state = [this](int slot) {
    appendLog(QStringLiteral("save state slot %1").arg(slot));
    State::Save(Core::System::GetInstance(), static_cast<u32>(slot));
  };
  actions.load_state = [this](int slot) {
    appendLog(QStringLiteral("load state slot %1").arg(slot));
    State::Load(Core::System::GetInstance(), static_cast<u32>(slot));
  };
  actions.refresh_wiimotes = [] { WiimoteReal::Refresh(); };
  actions.change_disc = [this](const std::string& disc) {
    appendLog(QStringLiteral("change disc: %1").arg(QString::fromStdString(disc)));
    auto& core_system = Core::System::GetInstance();
    core_system.GetDVDInterface().ChangeDisc(Core::CPUThreadGuard{core_system}, disc);
  };
  actions.reset = [this] {
    appendLog(QStringLiteral("reset"));
    Core::System::GetInstance().GetProcessorInterface().ResetButton_Tap();
  };
  actions.open_settings = [this] { openSettingsOverGame(); };
  actions.open_skylanders = [this] { openPageOverGame(QStringLiteral("skylanders")); };
  actions.open_infinity = [this] { openPageOverGame(QStringLiteral("infinity")); };
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

  auto boot = system_menu ?
                  std::make_unique<BootParameters>(BootParameters::NANDTitle{Titles::SYSTEM_MENU}) :
                  BootParameters::GenerateFromFile(path.toStdString());
  if (riivolution && boot && std::holds_alternative<BootParameters::Disc>(boot->parameters))
  {
    const DiscIO::Volume& volume = *std::get<BootParameters::Disc>(boot->parameters).volume;
    auto patches = DiscIO::Riivolution::GenerateRiivolutionPatchesFromConfig(
        File::GetUserPath(D_RIIVOLUTION_IDX), volume.GetGameID(), volume.GetRevision(),
        volume.GetDiscNumber());
    appendLog(QStringLiteral("Riivolution: %1 patches").arg(patches.size()));
    AddRiivolutionPatches(boot.get(), std::move(patches));
  }
  if (!BootManager::BootCore(system, std::move(boot), wsi))
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
  if (m_over_game > 0 || m_settings_over_game)
  {
    m_over_game = 0;
    m_settings_over_game = false;
    m_resume_after_over_game = false;
    emit overGameChanged();
  }
  UpdateHotkeysEnabled();
  if (m_launcher && !m_launcher->isVisible())
  {
    m_library_normal_requests = 0;
    m_launcher->show();
  }
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

  // Hotkeys while the game runs, as the Android app's Run starts and stops them around it.
  if (previous == Core::State::Starting &&
      (state == Core::State::Running || state == Core::State::Paused))
  {
    HotkeyScheduler::Start();
  }
  else if (state == Core::State::Stopping || state == Core::State::Uninitialized)
  {
    HotkeyScheduler::Stop();
  }

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

  // CPU boost (3.0 benchmark, 3.6 setting): on while the game runs, off when paused or stopped.
  const int boost = state == Core::State::Running ? Config::Get(CpuBoost::UT_CPU_BOOST) : 0;
  if (boost != m_cpu_boost)
  {
    m_cpu_boost = boost;
    appendLog(QStringLiteral("CPU boost %1 (uclamp_min) on %2 threads")
                  .arg(boost)
                  .arg(CpuBoost::Apply(boost)));
  }

  if (state == Core::State::Running || state == Core::State::Paused)
  {
    m_boot_reached_running = true;
    if (!m_fallback_note.isEmpty())
    {
      OSD::AddMessage(m_fallback_note.toStdString(), 10000, OSD::Color::YELLOW);
      m_fallback_note.clear();
    }
  }

  if (state == Core::State::Uninitialized)
  {
    // Vulkan (the default backend) failed to start: try OpenGL ES for this run instead of leaving
    // the user at the library with an error. The configured backend stays as it is, so Vulkan is
    // tried again next time; Settings (global or per game) can pick OpenGL ES for good.
    // The same when the driver lost the device during play (stage 4: Wind Waker hangs the GPU on
    // this phone's Vulkan driver). Nothing reaches the screen after that, so the game is stopped at
    // the first error (NoteAlert) and started again on OpenGL ES; its progress since the start is
    // lost either way.
    const bool vulkan = g_video_backend && g_video_backend->GetConfigName() == "Vulkan";
    const bool vulkan_failed =
        s_video_backend_failed.exchange(false) && !m_boot_reached_running && vulkan;
    const bool device_lost = s_device_lost.exchange(false) && vulkan;
    m_fallback_note.clear();
    FinishStop();
    if ((vulkan_failed || device_lost) && !m_boot_path.isEmpty())
    {
      appendLog(device_lost ?
                    QStringLiteral("Vulkan lost the GPU (%1 more errors not shown): restarting "
                                   "with OpenGL ES for this run")
                        .arg(s_device_lost_repeats.load()) :
                    QStringLiteral("Vulkan failed to start: retrying with OpenGL ES for this run"));
      const QString note =
          device_lost ? QStringLiteral("The GPU driver stopped responding on Vulkan: this game "
                                       "restarted on OpenGL ES. Game settings can choose it for "
                                       "good.") :
                        QStringLiteral("Vulkan failed to start: this game runs on OpenGL ES.");
      const QString path = m_boot_path;
      const bool riivolution = m_boot_riivolution;
      QTimer::singleShot(0, this, [this, path, riivolution, note] {
        Boot(path, QStringLiteral("OGL"), riivolution);
        if (m_game_window)
          m_fallback_note = note;
      });
    }
  }
}

void Emulator::OnApplicationStateChanged(Qt::ApplicationState state)
{
  appendLog(QStringLiteral("application state: %1").arg(AppStateName(state)));
  UpdateHotkeysEnabled();
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

void Emulator::openSettingsOverGame()
{
  if (m_settings_over_game || !m_game_window || !m_launcher)
    return;
  m_settings_over_game = true;
  BeginOverGame(QStringLiteral("settings"));
  emit settingsRequested();
}

void Emulator::openPageOverGame(const QString& page)
{
  if (m_settings_over_game || !m_game_window || !m_launcher)
    return;
  m_settings_over_game = true;
  BeginOverGame(page);
  emit pageOverGameRequested(page);
}

void Emulator::returnToGame()
{
  if (!m_settings_over_game)
    return;
  m_settings_over_game = false;
  EndOverGame(QStringLiteral("settings"));
  emit settingsClosed();
}

void Emulator::BeginOverGame(const QString& reason, bool pause)
{
  if (!m_game_window || !m_launcher)
    return;
  auto& system = Core::System::GetInstance();
  if (m_over_game++ == 0)
  {
    m_resume_after_over_game = pause && Core::GetState(system) == Core::State::Running;
    if (m_resume_after_over_game)
      Core::SetState(system, Core::State::Paused);
    // The game window stays mapped (its surface is the video backend's); the library window is
    // shown on top of it, as Lomiri stacks an app's windows.
    m_launcher->show();
    m_launcher->raise();
    m_launcher->requestActivate();
    emit overGameChanged();
    UpdateHotkeysEnabled();
  }
  appendLog(QStringLiteral("library over the game: %1 (%2 open)").arg(reason).arg(m_over_game));
}

void Emulator::UpdateHotkeysEnabled()
{
  HotkeyScheduler::SetEnabled(m_over_game == 0 &&
                              qGuiApp->applicationState() == Qt::ApplicationActive);
}

void Emulator::EndOverGame(const QString& reason)
{
  if (m_over_game == 0)
    return;
  appendLog(QStringLiteral("library over the game closed: %1 (%2 left)").arg(reason).arg(m_over_game - 1));
  if (--m_over_game > 0)
    return;
  emit overGameChanged();
  UpdateHotkeysEnabled();
  if (!m_game_window)
    return;
  if (m_launcher && m_hide_launcher)
    m_launcher->hide();
  m_game_window->requestActivate();
  auto& system = Core::System::GetInstance();
  if (m_resume_after_over_game && Core::GetState(system) == Core::State::Paused)
    Core::SetState(system, Core::State::Running);
  m_resume_after_over_game = false;
}

bool Emulator::AskFromAnyThread(const char* caption, const char* text, bool yes_no,
                                Common::MsgType style)
{
  Emulator* const instance = s_instance;
  if (!instance)
    return true;  // before the UI exists: continue, as the port did before 6.1
  const QString caption_text = QString::fromUtf8(caption);
  const QString message = QString::fromUtf8(text);
  if (QThread::currentThread() == instance->thread())
    return instance->Ask(caption_text, message, yes_no, style, AskingThread::UI);

  // As DolphinQt's handler: while the UI thread asks for the CPU or GPU thread, it declares itself
  // that thread, so a call that would wait for it (Core::CPUThreadGuard, ...) does not deadlock.
  const bool cpu_thread = Core::IsCPUThread();
  const bool gpu_thread = Core::IsGPUThread();
  bool answer = false;
  QMetaObject::invokeMethod(
      instance,
      [&] {
        if (cpu_thread)
          Core::DeclareAsCPUThread();
        if (gpu_thread)
          Core::DeclareAsGPUThread();
        answer = instance->Ask(caption_text, message, yes_no, style,
                               cpu_thread ? AskingThread::CPU :
                               gpu_thread ? AskingThread::GPU :
                                            AskingThread::Other);
        if (cpu_thread)
          Core::UndeclareAsCPUThread();
        if (gpu_thread)
          Core::UndeclareAsGPUThread();
      },
      Qt::BlockingQueuedConnection);
  return answer;
}

bool Emulator::Ask(const QString& caption, const QString& text, bool yes_no, Common::MsgType style,
                   AskingThread thread)
{
  auto& system = Core::System::GetInstance();
  const Core::State state = Core::GetState(system);
  const bool game_shown = m_game_window && m_overlay && !m_settings_over_game &&
                          (state == Core::State::Running || state == Core::State::Paused);
  // The thread that draws the game: the GPU thread in dual core, the CPU thread in single core.
  const bool dual_core = system.IsDualCoreMode();
  const bool drawing_thread_asks = dual_core ? thread == AskingThread::GPU :
                                               thread == AskingThread::CPU;

  // Where the question goes (S6.1-M3: Lomiri has no xdg-activation, so the library window shown
  // during a game without a recent tap can stay behind it):
  // - The thread that draws the game asked: nothing can be drawn while it waits, so as 3.6 did,
  //   the message goes on screen once it returns, and the game continues ("Yes" to a question,
  //   Dolphin's "Ignore and continue").
  // - A game is on screen: in the picture, over everything (TouchOverlay::ShowQuestion). If the
  //   CPU thread asked (dual core), the game presents nothing meanwhile, so redraws are pushed
  //   through the paused-redraw path; this thread is declared the CPU thread while it asks, so
  //   Core::RunOnCPUThread runs them here.
  // - Otherwise (no game, a boot, the settings over the game): a dialog in the library window.
  if (m_game_window && drawing_thread_asks)
  {
    std::string first_line = text.toStdString();
    first_line = first_line.substr(0, first_line.find("\n\n"));
    OSD::AddMessage(caption.toStdString() + ": " + first_line, 8000,
                    style == Common::MsgType::Information ? OSD::Color::YELLOW : OSD::Color::RED);
    appendLog(QStringLiteral("alert from the thread that draws the game: shown on screen, continuing"));
    return true;
  }

  const int id = m_next_question_id++;
  m_answers[id] = -1;
  const bool warning = style == Common::MsgType::Warning;
  QTimer redraw;
  int redraws = 0;
  if (game_shown)
  {
    appendLog(QStringLiteral("question %1 in the game picture").arg(id));
    m_overlay->ShowQuestion(id, caption.toStdString(), text.toStdString(), yes_no, warning,
                            [this](int answered, int button) { answerQuestion(answered, button); });
    if (thread == AskingThread::CPU || state == Core::State::Paused)
    {
      redraw.setInterval(33);
      connect(&redraw, &QTimer::timeout, this, [&system, &redraws] {
        ++redraws;
        Core::RunOnCPUThread(system, [] {
          AsyncRequests::GetInstance()->PushEvent([] {
            if (!g_presenter)
              return;
            PresentInfo info{};
            GetVideoEvents().before_present_event.Trigger(info);
            g_presenter->Present();
          });
        });
      });
      redraw.start();
    }
  }
  else
  {
    BeginOverGame(QStringLiteral("question %1").arg(id), false);
    if (m_launcher && !m_launcher->isVisible())
      m_launcher->show();
    emit questionAsked(id, caption, text, yes_no, warning);
  }

  // A nested event loop, as a modal dialog's: the UI keeps running while this waits. Quitting the
  // app (SIGTERM) ends every event loop, which counts as No / OK.
  QEventLoop loop;
  const auto connection = connect(this, &Emulator::questionAnswered, &loop, [&loop, id](int answered) {
    if (answered == id)
      loop.quit();
  });
  if (m_answers[id] < 0)
    loop.exec();
  disconnect(connection);
  redraw.stop();
  if (redraws > 0)
    appendLog(QStringLiteral("question %1: %2 redraws pushed while it waited").arg(id).arg(redraws));
  const int button = m_answers[id];
  m_answers.erase(id);
  if (game_shown)
  {
    if (m_overlay)
      m_overlay->CloseQuestion(id);
  }
  else
  {
    EndOverGame(QStringLiteral("question %1").arg(id));
  }

  appendLog(QStringLiteral("question %1 answered: %2")
                .arg(id)
                .arg(button == 1 ? QStringLiteral("yes") :
                     button == 2 ? QStringLiteral("ignore for this session") :
                     button == 0 ? (yes_no ? QStringLiteral("no") : QStringLiteral("ok")) :
                                   QStringLiteral("none (quitting)")));
  if (button == 2)
  {
    Config::SetCurrent(Config::MAIN_USE_PANIC_HANDLERS, false);
    return true;
  }
  return yes_no ? button == 1 : true;
}

void Emulator::answerQuestion(int id, int button)
{
  const auto it = m_answers.find(id);
  if (it == m_answers.end() || it->second >= 0)
    return;
  it->second = button;
  emit questionAnswered(id);
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

void Emulator::OnGameWindowSizeChanged(int width, int height, float scale)
{
  appendLog(QStringLiteral("game window size %1x%2 (scale %3)").arg(width).arg(height).arg(scale));
  g_controller_interface.SetRenderSurfaceSize(width, height, scale);
  // Stage 6.6: the phone's motion axes turn with a landscape game.
  if (PhoneSensors* const sensors = PhoneSensors::Instance())
    sensors->SetLandscape(width > height);
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

bool Emulator::NoteAlert(const char* text)
{
  const std::string_view message(text);
  // Core::EmuThread's message when the backend's Initialize fails (not translated).
  if (message == "Failed to initialize video backend!")
    s_video_backend_failed = true;
  // The Vulkan backend's "Failed to submit command buffer: VK_ERROR_DEVICE_LOST" and the like:
  // every later submit fails too. Stop the game at the first one.
  if (message.find("VK_ERROR_DEVICE_LOST") != std::string_view::npos)
  {
    if (s_device_lost.exchange(true))
    {
      ++s_device_lost_repeats;
      return true;
    }
    RequestStopFromAnyThread();
  }
  return false;
}

// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/WiiSystem.h"

#include <QMetaObject>

#include <fmt/format.h>

#include "Core/CommonTitles.h"
#include "Core/Core.h"
#include "Core/HW/WiiSave.h"
#include "Core/IOS/ES/ES.h"
#include "Core/IOS/IOS.h"
#include "Core/System.h"
#include "Core/WiiUtils.h"
#include "DiscIO/Enums.h"
#include "DiscIO/NANDImporter.h"
#include "DolphinUT/Emulator.h"

namespace
{
// The Android app's texts (res/values/strings.xml), as SystemUpdateResultFragment shows them.
// DiscReadFailed has the MissingUpdatePartition text there and in DolphinQt (WiiUpdate.cpp).
QString UpdateTitle(WiiUtils::UpdateResult result)
{
  using R = WiiUtils::UpdateResult;
  switch (result)
  {
  case R::Succeeded:
  case R::AlreadyUpToDate:
    return QStringLiteral("Update completed");
  case R::Cancelled:
    return QStringLiteral("Update cancelled");
  default:
    return QStringLiteral("Update failed");
  }
}

QString UpdateMessage(WiiUtils::UpdateResult result)
{
  using R = WiiUtils::UpdateResult;
  switch (result)
  {
  case R::Succeeded:
    return QStringLiteral("The emulated Wii console has been updated.");
  case R::AlreadyUpToDate:
    return QStringLiteral("The emulated Wii console is already up-to-date.");
  case R::RegionMismatch:
    return QStringLiteral("The game's region does not match your console's. To avoid issues with "
                          "the system menu, it is not possible to update the emulated console "
                          "using this disc.");
  case R::MissingUpdatePartition:
  case R::DiscReadFailed:
    return QStringLiteral("The game disc does not contain any usable update information.");
  case R::ServerFailed:
    return QStringLiteral("Could not download update information from Nintendo. Please check "
                          "your Internet connection and try again.");
  case R::DownloadFailed:
    return QStringLiteral("Could not download update files from Nintendo. Please check your "
                          "Internet connection and try again.");
  case R::ImportFailed:
    return QStringLiteral("Could not install an update to the Wii system memory. Please refer to "
                          "logs for more information.");
  case R::Cancelled:
    return QStringLiteral("The update has been cancelled. It is strongly recommended to finish it "
                          "in order to avoid inconsistent system software versions.");
  default:
    return QStringLiteral("Unknown result.");
  }
}

bool UpdateOk(WiiUtils::UpdateResult result)
{
  return result == WiiUtils::UpdateResult::Succeeded ||
         result == WiiUtils::UpdateResult::AlreadyUpToDate;
}
}  // namespace

WiiSystem::WiiSystem(QObject* parent) : QObject(parent)
{
}

WiiSystem::~WiiSystem()
{
  m_cancel = true;
  {
    std::lock_guard lock(m_answer_mutex);
    m_quitting = true;
  }
  m_answer_cv.notify_all();
  if (m_thread.joinable())
    m_thread.join();
}

void WiiSystem::refresh()
{
  // A second IOS beside a running game's would share its NAND files; the library is hidden then.
  if (m_busy || !Core::IsUninitialized(Core::System::GetInstance()))
    return;
  IOS::HLE::Kernel ios;
  const auto tmd = ios.GetESCore().FindInstalledTMD(Titles::SYSTEM_MENU);
  const bool installed = tmd.IsValid();
  const bool vwii = installed && tmd.IsvWii();
  const QString version =
      installed ?
          QString::fromStdString(DiscIO::GetSysMenuVersionString(tmd.GetTitleVersion(), vwii)) :
          QString();
  if (installed != m_menu_installed || version != m_menu_version || vwii != m_menu_vwii)
  {
    m_menu_installed = installed;
    m_menu_version = version;
    m_menu_vwii = vwii;
    emit menuChanged();
  }
}

bool WiiSystem::Run(const QString& task, std::function<Result()> work)
{
  if (m_busy || !Core::IsUninitialized(Core::System::GetInstance()))
  {
    Emulator::LogFromAnyThread(QStringLiteral("wii %1: not started (%2)")
                                   .arg(task, m_busy ? QStringLiteral("busy") :
                                                       QStringLiteral("a game runs")));
    return false;
  }
  if (m_thread.joinable())
    m_thread.join();
  m_busy = true;
  m_task = task;
  m_cancel = false;
  m_progress_text.clear();
  m_progress = 0;
  m_progress_total = 0;
  emit busyChanged();
  emit progressChanged();
  Emulator::LogFromAnyThread(QStringLiteral("wii %1: started").arg(task));
  m_thread = std::thread([this, task, work = std::move(work)] {
    const Result result = work();
    QMetaObject::invokeMethod(this, [this, task, result] {
      Emulator::LogFromAnyThread(QStringLiteral("wii %1: %2%3")
                                     .arg(task, result.ok ? "done" : "failed",
                                          result.message.isEmpty() ?
                                              QString() :
                                              QStringLiteral(" (%1)").arg(result.message)));
      m_busy = false;
      emit busyChanged();
      refresh();
      emit finished(task, result.ok, result.title, result.message);
    });
  });
  return true;
}

void WiiSystem::SetProgress(const QString& text, int progress, int total)
{
  QMetaObject::invokeMethod(this, [this, text, progress, total] {
    m_progress_text = text;
    m_progress = progress;
    m_progress_total = total;
    emit progressChanged();
  });
}

QString WiiSystem::AskFromWorker(void (WiiSystem::*signal)())
{
  {
    std::lock_guard lock(m_answer_mutex);
    m_answer.reset();
  }
  QMetaObject::invokeMethod(this, [this, signal] { emit(this->*signal)(); });
  std::unique_lock lock(m_answer_mutex);
  m_answer_cv.wait(lock, [this] { return m_answer.has_value() || m_quitting; });
  return m_answer.value_or(QString());
}

void WiiSystem::answerOverwrite(bool overwrite)
{
  {
    std::lock_guard lock(m_answer_mutex);
    m_answer = overwrite ? QStringLiteral("yes") : QString();
  }
  m_answer_cv.notify_all();
}

void WiiSystem::provideKeysFile(const QString& path)
{
  {
    std::lock_guard lock(m_answer_mutex);
    m_answer = path;
  }
  m_answer_cv.notify_all();
}

void WiiSystem::cancel()
{
  if (!m_busy || m_cancel)
    return;
  m_cancel = true;
  Emulator::LogFromAnyThread(QStringLiteral("wii %1: cancelling").arg(m_task));
  emit progressChanged();
}

bool WiiSystem::installWad(const QString& path)
{
  // MainPresenter.installWAD.
  return Run(QStringLiteral("wad"), [path] {
    const bool ok = WiiUtils::InstallWAD(path.toStdString());
    return Result{ok, ok ? QStringLiteral("Done") : QStringLiteral("Failed"),
                  ok ? QStringLiteral("Successfully installed this title to the NAND.") :
                       QStringLiteral("Failed to install this title to the NAND.")};
  });
}

bool WiiSystem::importSave(const QString& path)
{
  // MainPresenter.importWiiSave: an existing save is overwritten only if the user agrees.
  return Run(QStringLiteral("save"), [this, path] {
    const auto can_overwrite = [this] { return !AskFromWorker(&WiiSystem::overwriteAsked).isEmpty(); };
    switch (WiiSave::Import(path.toStdString(), can_overwrite))
    {
    case WiiSave::CopyResult::Success:
      return Result{true, QStringLiteral("Done"), QStringLiteral("Successfully imported save file.")};
    case WiiSave::CopyResult::Cancelled:
      return Result{false, QStringLiteral("Cancelled"), QString()};
    case WiiSave::CopyResult::CorruptedSource:
      return Result{false, QStringLiteral("Failed"),
                    QStringLiteral("Failed to import save file. The given file appears to be "
                                   "corrupted or is not a valid Wii save.")};
    case WiiSave::CopyResult::TitleMissing:
      return Result{false, QStringLiteral("Failed"),
                    QStringLiteral("Failed to import save file. Please launch the game once, then "
                                   "try again.")};
    default:
      return Result{false, QStringLiteral("Failed"),
                    QStringLiteral("Failed to import save file. Your NAND may be corrupt, or "
                                   "something is preventing access to files within it.")};
    }
  });
}

bool WiiSystem::importNand(const QString& path)
{
  // MainPresenter.importNANDBin, with DolphinQt's progress, cancel and keys file
  // (MainWindow::OnImportNANDBackup). ImportNANDBin returns no result: it shows some failures as
  // Dolphin's alerts (stage 6.1 dialogs), logs others, and on success reports the extraction
  // complete once more at the very end, which tells the two apart.
  return Run(QStringLiteral("nand"), [this, path] {
    bool asked_keys = false;
    bool keys_given = true;
    bool complete = false;
    DiscIO::NANDImporter().ImportNANDBin(
        path.toStdString(),
        [this, &complete](DiscIO::NANDImporter::Step step, int cur, int max) {
          complete = step == DiscIO::NANDImporter::Step::Extracting && max > 0 && cur >= max;
          SetProgress(step == DiscIO::NANDImporter::Step::Loading ?
                          QStringLiteral("Loading NAND…") :
                          QStringLiteral("Extracting NAND…"),
                      cur, max);
          return m_cancel.load();
        },
        [this, &asked_keys, &keys_given] {
          asked_keys = true;
          const QString keys = AskFromWorker(&WiiSystem::keysFileAsked);
          keys_given = !keys.isEmpty();
          return keys.toStdString();
        });
    if (m_cancel)
      return Result{false, QStringLiteral("Cancelled"), QStringLiteral("The import was cancelled.")};
    if (asked_keys && !keys_given)
    {
      return Result{false, QStringLiteral("Cancelled"),
                    QStringLiteral("The NAND backup has no keys appended, and no keys file was "
                                   "chosen.")};
    }
    if (!complete)
    {
      return Result{false, QStringLiteral("Failed"),
                    QStringLiteral("The NAND backup could not be imported. See the App Log "
                                   "(library ⋮ → App Log).")};
    }
    return Result{true, QStringLiteral("Done"), QStringLiteral("The NAND backup was imported.")};
  });
}

bool WiiSystem::onlineUpdate(const QString& region)
{
  // SystemUpdateViewModel.startOnlineUpdate; DolphinQt then extracts the certificates.
  return Run(QStringLiteral("update"), [this, region] {
    const auto result = WiiUtils::DoOnlineUpdate(
        [this](size_t processed, size_t total, u64 title_id) {
          SetProgress(QString::fromStdString(
                          fmt::format("Updating title {:016x}…\nThis can take a while.", title_id)),
                      static_cast<int>(processed), static_cast<int>(total));
          return !m_cancel.load();
        },
        region.toStdString());
    if (UpdateOk(result))
      DiscIO::NANDImporter().ExtractCertificates();
    return Result{UpdateOk(result), UpdateTitle(result), UpdateMessage(result)};
  });
}

bool WiiSystem::discUpdate(const QString& path)
{
  // SystemUpdateViewModel.startDiscUpdate (the game properties' "Perform System Update").
  return Run(QStringLiteral("update"), [this, path] {
    const auto result = WiiUtils::DoDiscUpdate(
        [this](size_t processed, size_t total, u64 title_id) {
          SetProgress(QString::fromStdString(
                          fmt::format("Updating title {:016x}…\nThis can take a while.", title_id)),
                      static_cast<int>(processed), static_cast<int>(total));
          return !m_cancel.load();
        },
        path.toStdString());
    if (UpdateOk(result))
      DiscIO::NANDImporter().ExtractCertificates();
    return Result{UpdateOk(result), UpdateTitle(result), UpdateMessage(result)};
  });
}

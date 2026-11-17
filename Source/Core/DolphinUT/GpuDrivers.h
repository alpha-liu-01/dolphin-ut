// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVariant>

// Custom Vulkan drivers (stage 4, docs/ut-port/stage4-adreno.md), as the Android app's
// Graphics → GPU Driver: driver packages in the AdrenoTools format (a zip with meta.json and the
// driver library, e.g. Mesa Turnip from K11MCH1/AdrenoToolsDrivers) are installed into
// <user folder>/GpuDrivers/<name-vVersion>/, and one is chosen for Vulkan.
//
// The driver is loaded by libhybris, which starts with the app (VulkanDriver.h): the choice
// applies from the next start. It is kept in Dolphin.ini, [UbuntuTouch] GpuDriver (the folder
// name; empty: the phone's own driver). Dolphin's DriverLibName setting is the Android app's
// (libadrenotools) and stays unused here.
class GpuDrivers : public QObject
{
  Q_OBJECT
  // [{id, name, version, driverVersion, vendor, author, description}], by name.
  Q_PROPERTY(QVariantList drivers READ GetDrivers NOTIFY changed)
  // The chosen driver's id, or "" for the system driver.
  Q_PROPERTY(QString selected READ GetSelected NOTIFY changed)
  // The driver this run uses: its id, a folder (DOLPHIN_UT_GPU_DRIVER), or "" for the system's.
  Q_PROPERTY(QString active READ GetActive CONSTANT)
  // Why the chosen driver is not in use, if it is not.
  Q_PROPERTY(QString activeError READ GetActiveError CONSTANT)
  Q_PROPERTY(QString folder READ GetFolder CONSTANT)

public:
  // In main(), after the user folder is set and before QGuiApplication (libhybris starts with
  // Qt's EGL). DOLPHIN_UT_GPU_DRIVER (a folder, for tests; development builds only) overrides the setting. Returns a line
  // for the log, or "" when the system driver is used as chosen.
  static std::string ApplyAtStart();
  // Stage 6.8: whether a file is a driver package (a zip with meta.json), for files that come from
  // other apps (content-hub). From any thread.
  static bool IsDriverPackage(const QString& path);

  explicit GpuDrivers(QObject* parent = nullptr);

  QVariantList GetDrivers() const;
  QString GetSelected() const;
  QString GetActive() const;
  QString GetActiveError() const;
  QString GetFolder() const;

  // Installs the package at PATH: {ok, id, name, error}. A package with the same name and version
  // replaces the installed one.
  Q_INVOKABLE QVariantMap install(const QString& path);
  // Chooses ID ("" for the system driver) for the next start.
  Q_INVOKABLE void select(const QString& id);
  // Removes the installed driver ID (and chooses the system driver if it was chosen).
  Q_INVOKABLE bool remove(const QString& id);

signals:
  void changed();
};

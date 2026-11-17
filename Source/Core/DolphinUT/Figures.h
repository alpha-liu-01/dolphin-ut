// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

#include <QObject>
#include <QString>
#include <QVariantList>

// The Skylanders Portal and the Infinity Base during a game (stage 6.8,
// docs/ut-port/stage6.8-leftovers.md), as the Android app's in-game "Skylanders Portal" and
// "Infinity Base" dialogs (EmulationActivity, Source/Android/jni/SkylanderConfig.cpp and
// InfinityConfig.cpp): figure slots to load a figure file into, create a new figure in, or clear.
// Dolphin's emulated portal and base do the rest (Config → Wii turns them on).
class Figures : public QObject
{
  Q_OBJECT
  // [{label}] for the eight Skylanders slots and the nine Infinity positions ("Empty" or the figure).
  Q_PROPERTY(QVariantList skylanderSlots READ GetSkylanderSlots NOTIFY changed)
  Q_PROPERTY(QVariantList infinitySlots READ GetInfinitySlots NOTIFY changed)
  // Where created figures go (as DolphinQt: User/Skylanders; and User/Infinity).
  Q_PROPERTY(QString skylanderFolder READ GetSkylanderFolder CONSTANT)
  Q_PROPERTY(QString infinityFolder READ GetInfinityFolder CONSTANT)

public:
  explicit Figures(QObject* parent = nullptr);

  QVariantList GetSkylanderSlots() const;
  QVariantList GetInfinitySlots() const;
  QString GetSkylanderFolder() const;
  QString GetInfinityFolder() const;

  // The known figures for "Create": [{name, id, variant}] / [{name, number}], sorted by name.
  Q_INVOKABLE QVariantList skylanderList() const;
  Q_INVOKABLE QVariantList infinityList() const;

  // Each returns "" when done, else why (the Android app's "Failed to load the figure" and the like).
  Q_INVOKABLE QString loadSkylander(int slot, const QString& path);
  Q_INVOKABLE QString createSkylander(int slot, int id, int variant);
  Q_INVOKABLE void clearSkylander(int slot);
  Q_INVOKABLE QString loadInfinity(int position, const QString& path);
  Q_INVOKABLE QString createInfinity(int position, int number);
  Q_INVOKABLE void clearInfinity(int position);

  // A game ended: the figures are taken off the portal and the base (they outlive a game), and the
  // slots empty.
  void Reset();

signals:
  void changed();

private:
  struct SkylanderSlot
  {
    int portal_slot = -1;  // the portal's slot (LoadSkylander's result), -1 empty
    QString label;
  };
  QString PutSkylander(int slot, const std::string& path);
  QString PutInfinity(int position, const std::string& path);

  std::array<SkylanderSlot, 8> m_skylanders;
  std::array<QString, 9> m_infinity;
};

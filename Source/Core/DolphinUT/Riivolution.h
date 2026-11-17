// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

#include <QObject>
#include <QString>
#include <QVariantList>

#include "DiscIO/RiivolutionParser.h"

// Start with Riivolution Patches (stage 6.4, docs/ut-port/stage6.4-game-tools.md), as the Android
// app's RiivolutionBootActivity (Source/Android/jni/RiivolutionPatches.cpp): the patch XMLs in
// Load/Riivolution/riivolution/ that fit the game, each option's choice (saved to
// riivolution/config/<ID4>.xml), then a start with the patches (Emulator::startWithRiivolution).
class Riivolution : public QObject
{
  Q_OBJECT
  // [{name (the XML's file name), sections: [{name, options: [{name, choices: [...], selected}]}]}];
  // selected 0 is "Disabled", n the nth choice.
  Q_PROPERTY(QVariantList discs READ GetDiscs NOTIFY changed)
  // Where the patches' files go (the Android app's "SD Root"), and where the XMLs go.
  Q_PROPERTY(QString sdRoot READ GetSdRoot CONSTANT)
  Q_PROPERTY(QString xmlFolder READ GetXmlFolder CONSTANT)

public:
  explicit Riivolution(QObject* parent = nullptr);

  QVariantList GetDiscs() const;
  QString GetSdRoot() const;
  QString GetXmlFolder() const;

  Q_INVOKABLE void load(const QString& game_id, int revision, int disc_number);
  Q_INVOKABLE void setChoice(int disc, int section, int option, int choice);
  Q_INVOKABLE void save();

signals:
  void changed();

private:
  std::string m_game_id;
  std::vector<DiscIO::Riivolution::Disc> m_discs;
};

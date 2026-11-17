// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/Figures.h"

#include <algorithm>
#include <memory>

#include <QDir>
#include <QVariantMap>

#include <fmt/format.h>

#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Core/IOS/USB/Emulated/Infinity.h"
#include "Core/IOS/USB/Emulated/Skylanders/Skylander.h"
#include "Core/IOS/USB/Emulated/Skylanders/SkylanderFigure.h"
#include "Core/System.h"
#include "DolphinUT/Emulator.h"

namespace
{
using IOS::HLE::USB::FigureUIPosition;

// The Android app's labels (skylander_slot, infinity_*_label).
const char* const INFINITY_POSITIONS[9] = {
    "Play Set/Power Disc", "Power Disc Two", "Power Disc Three", "Player One",
    "Player One Ability One", "Player One Ability Two", "Player Two", "Player Two Ability One",
    "Player Two Ability Two"};

QString FreePath(const QString& folder, const QString& name, const QString& extension)
{
  QDir().mkpath(folder);
  QString path = QStringLiteral("%1/%2.%3").arg(folder, name, extension);
  for (int i = 2; QFileInfo::exists(path); ++i)
    path = QStringLiteral("%1/%2 (%3).%4").arg(folder, name).arg(i).arg(extension);
  return path;
}

QString SafeName(QString name)
{
  return name.replace(QLatin1Char('/'), QLatin1Char('-'));
}
}  // namespace

Figures::Figures(QObject* parent) : QObject(parent)
{
}

QVariantList Figures::GetSkylanderSlots() const
{
  QVariantList result;
  for (std::size_t i = 0; i < m_skylanders.size(); ++i)
  {
    result.append(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("Skylander %1").arg(i + 1)},
        {QStringLiteral("label"), m_skylanders[i].label.isEmpty() ? QStringLiteral("Empty") :
                                                                    m_skylanders[i].label}});
  }
  return result;
}

QVariantList Figures::GetInfinitySlots() const
{
  QVariantList result;
  for (std::size_t i = 0; i < m_infinity.size(); ++i)
  {
    result.append(QVariantMap{
        {QStringLiteral("name"), QString::fromLatin1(INFINITY_POSITIONS[i])},
        {QStringLiteral("label"),
         m_infinity[i].isEmpty() ? QStringLiteral("Empty") : m_infinity[i]}});
  }
  return result;
}

QString Figures::GetSkylanderFolder() const
{
  return QString::fromStdString(File::GetUserPath(D_USER_IDX)) + QStringLiteral("Skylanders");
}

QString Figures::GetInfinityFolder() const
{
  return QString::fromStdString(File::GetUserPath(D_USER_IDX)) + QStringLiteral("Infinity");
}

QVariantList Figures::skylanderList() const
{
  QVariantList list;
  for (const auto& [ids, data] : IOS::HLE::USB::list_skylanders)
  {
    list.append(QVariantMap{{QStringLiteral("name"), QString::fromUtf8(data.name)},
                            {QStringLiteral("id"), ids.first},
                            {QStringLiteral("variant"), ids.second}});
  }
  std::ranges::sort(list, [](const QVariant& a, const QVariant& b) {
    return a.toMap().value(QStringLiteral("name")).toString().compare(
               b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
  });
  return list;
}

QVariantList Figures::infinityList() const
{
  QVariantList list;
  for (const auto& [name, number] : IOS::HLE::USB::InfinityBase::GetFigureList())
  {
    list.append(QVariantMap{{QStringLiteral("name"), QString::fromUtf8(name)},
                            {QStringLiteral("number"), static_cast<qlonglong>(number)}});
  }
  std::ranges::sort(list, [](const QVariant& a, const QVariant& b) {
    return a.toMap().value(QStringLiteral("name")).toString().compare(
               b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
  });
  return list;
}

// SkylanderConfig.loadSkylander: the file read, the slot's figure removed, the new one placed
// (the portal picks its own slot), its name from the known list.
QString Figures::PutSkylander(int slot, const std::string& path)
{
  File::IOFile file(path, "r+b");
  if (!file)
    return QStringLiteral("Failed to open the file.");
  std::array<u8, IOS::HLE::USB::FIGURE_SIZE> data;
  if (!file.ReadBytes(data.data(), data.size()))
    return QStringLiteral("Not a Skylander file (too short).");
  file.Seek(0, File::SeekOrigin::Begin);
  auto& portal = Core::System::GetInstance().GetSkylanderPortal();
  clearSkylander(slot);
  const auto ids = portal.CalculateIDs(data);
  const auto known = IOS::HLE::USB::list_skylanders.find(ids);
  const u8 portal_slot =
      portal.LoadSkylander(std::make_unique<IOS::HLE::USB::SkylanderFigure>(std::move(file)));
  if (portal_slot == 0xFF)
    return QStringLiteral("The portal is full.");
  m_skylanders[slot] = {portal_slot, known != IOS::HLE::USB::list_skylanders.end() ?
                                         QString::fromUtf8(known->second.name) :
                                         QStringLiteral("Unknown (%1 %2)").arg(ids.first).arg(ids.second)};
  Emulator::LogFromAnyThread(QStringLiteral("skylanders: slot %1: %2 (portal slot %3)")
                                 .arg(slot + 1)
                                 .arg(m_skylanders[slot].label)
                                 .arg(portal_slot));
  emit changed();
  return {};
}

QString Figures::loadSkylander(int slot, const QString& path)
{
  if (slot < 0 || slot >= int(m_skylanders.size()))
    return QStringLiteral("No such slot.");
  return PutSkylander(slot, path.toStdString());
}

QString Figures::createSkylander(int slot, int id, int variant)
{
  if (slot < 0 || slot >= int(m_skylanders.size()))
    return QStringLiteral("No such slot.");
  const auto known = IOS::HLE::USB::list_skylanders.find({u16(id), u16(variant)});
  const QString name = known != IOS::HLE::USB::list_skylanders.end() ?
                           QString::fromUtf8(known->second.name) :
                           QStringLiteral("Unknown(%1 %2)").arg(id).arg(variant);
  const QString path = FreePath(GetSkylanderFolder(), SafeName(name), QStringLiteral("sky"));
  {
    // SkylanderConfig.createSkylander.
    IOS::HLE::USB::SkylanderFigure figure(path.toStdString());
    if (!figure.Create(u16(id), u16(variant)))
      return QStringLiteral("Failed to create the figure file.");
    figure.Close();
  }
  Emulator::LogFromAnyThread(QStringLiteral("skylanders: created %1").arg(path));
  return PutSkylander(slot, path.toStdString());
}

void Figures::clearSkylander(int slot)
{
  if (slot < 0 || slot >= int(m_skylanders.size()) || m_skylanders[slot].portal_slot < 0)
    return;
  Core::System::GetInstance().GetSkylanderPortal().RemoveSkylander(
      static_cast<u8>(m_skylanders[slot].portal_slot));
  m_skylanders[slot] = {};
  emit changed();
}

// InfinityConfig.loadFigure.
QString Figures::PutInfinity(int position, const std::string& path)
{
  File::IOFile file(path, "r+b");
  if (!file)
    return QStringLiteral("Failed to open the file.");
  std::array<u8, IOS::HLE::USB::INFINITY_NUM_BLOCKS * IOS::HLE::USB::INFINITY_BLOCK_SIZE> data{};
  if (!file.ReadBytes(data.data(), data.size()))
    return QStringLiteral("Not an Infinity figure file (too short).");
  auto& base = Core::System::GetInstance().GetInfinityBase();
  base.RemoveFigure(static_cast<FigureUIPosition>(position));
  m_infinity[position] = QString::fromStdString(
      base.LoadFigure(data, std::move(file), static_cast<FigureUIPosition>(position)));
  Emulator::LogFromAnyThread(QStringLiteral("infinity: %1: %2")
                                 .arg(QString::fromLatin1(INFINITY_POSITIONS[position]),
                                      m_infinity[position]));
  emit changed();
  return {};
}

QString Figures::loadInfinity(int position, const QString& path)
{
  if (position < 0 || position >= int(m_infinity.size()))
    return QStringLiteral("No such position.");
  return PutInfinity(position, path.toStdString());
}

QString Figures::createInfinity(int position, int number)
{
  if (position < 0 || position >= int(m_infinity.size()))
    return QStringLiteral("No such position.");
  auto& base = Core::System::GetInstance().GetInfinityBase();
  const std::string found = base.FindFigure(static_cast<u32>(number));
  const QString name = found.empty() ? QStringLiteral("Unknown(%1)").arg(number) :
                                       QString::fromStdString(found);
  const QString path = FreePath(GetInfinityFolder(), SafeName(name), QStringLiteral("bin"));
  // InfinityConfig.createFigure.
  if (!base.CreateFigure(path.toStdString(), static_cast<u32>(number)))
    return QStringLiteral("Failed to create the figure file.");
  Emulator::LogFromAnyThread(QStringLiteral("infinity: created %1").arg(path));
  return PutInfinity(position, path.toStdString());
}

void Figures::clearInfinity(int position)
{
  if (position < 0 || position >= int(m_infinity.size()) || m_infinity[position].isEmpty())
    return;
  Core::System::GetInstance().GetInfinityBase().RemoveFigure(
      static_cast<FigureUIPosition>(position));
  m_infinity[position].clear();
  emit changed();
}

void Figures::Reset()
{
  // The portal and the base belong to Core::System and outlive a game: empty them with the slots.
  for (int i = 0; i < int(m_skylanders.size()); ++i)
    clearSkylander(i);
  for (int i = 0; i < int(m_infinity.size()); ++i)
    clearInfinity(i);
}

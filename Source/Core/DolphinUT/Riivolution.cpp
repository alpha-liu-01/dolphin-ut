// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/Riivolution.h"

#include <optional>
#include <string_view>

#include <QStringList>
#include <QVariantMap>

#include <fmt/format.h>

#include "Common/FileSearch.h"
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"
#include "DolphinUT/Emulator.h"

namespace
{
QString Q(const std::string& text)
{
  return QString::fromStdString(text);
}

// RiivolutionPatches.cpp's LoadConfigXML: the choices are kept per 4-character game ID, which only
// makes sense for standard game IDs.
std::optional<std::string> ConfigPath(std::string_view game_id)
{
  if (!(game_id.size() == 4 || game_id.size() == 6))
    return std::nullopt;
  return fmt::format("{}/riivolution/config/{}.xml", File::GetUserPath(D_RIIVOLUTION_IDX),
                     game_id.substr(0, 4));
}
}  // namespace

Riivolution::Riivolution(QObject* parent) : QObject(parent)
{
}

QString Riivolution::GetSdRoot() const
{
  // RiivolutionBootActivity: the Load folder's Riivolution folder.
  std::string root = File::GetUserPath(D_RIIVOLUTION_IDX);
  if (root.ends_with('/'))
    root.pop_back();
  return Q(root);
}

QString Riivolution::GetXmlFolder() const
{
  return Q(File::GetUserPath(D_RIIVOLUTION_IDX) + "riivolution");
}

void Riivolution::load(const QString& game_id, int revision, int disc_number)
{
  // RiivolutionPatches.loadConfigImpl.
  m_game_id = game_id.toStdString();
  m_discs.clear();
  const std::string folder = File::GetUserPath(D_RIIVOLUTION_IDX) + "riivolution";
  std::optional<DiscIO::Riivolution::Config> config;
  if (const auto path = ConfigPath(m_game_id))
    config = DiscIO::Riivolution::ParseConfigFile(*path);
  if (File::IsDirectory(folder))
  {
    for (const std::string& path : Common::DoFileSearch({folder}, {".xml"}))
    {
      auto parsed = DiscIO::Riivolution::ParseFile(path);
      if (!parsed ||
          !parsed->IsValidForGame(m_game_id, static_cast<u16>(revision), static_cast<u8>(disc_number)))
      {
        continue;
      }
      if (config)
        DiscIO::Riivolution::ApplyConfigDefaults(&*parsed, *config);
      m_discs.emplace_back(std::move(*parsed));
    }
  }
  Emulator::LogFromAnyThread(QStringLiteral("riivolution %1: %2 patch files for this game")
                                 .arg(game_id)
                                 .arg(m_discs.size()));
  emit changed();
}

QVariantList Riivolution::GetDiscs() const
{
  QVariantList discs;
  for (const auto& disc : m_discs)
  {
    std::string name, extension;
    SplitPath(disc.m_xml_path, nullptr, &name, &extension);
    QVariantList sections;
    for (const auto& section : disc.m_sections)
    {
      QVariantList options;
      for (const auto& option : section.m_options)
      {
        QStringList choices;
        for (const auto& choice : option.m_choices)
          choices.append(Q(choice.m_name));
        options.append(QVariantMap{{QStringLiteral("name"), Q(option.m_name)},
                                   {QStringLiteral("choices"), choices},
                                   {QStringLiteral("selected"), int(option.m_selected_choice)}});
      }
      sections.append(QVariantMap{{QStringLiteral("name"), Q(section.m_name)},
                                  {QStringLiteral("options"), options}});
    }
    discs.append(QVariantMap{{QStringLiteral("name"), Q(name + extension)},
                             {QStringLiteral("sections"), sections}});
  }
  return discs;
}

void Riivolution::setChoice(int disc, int section, int option, int choice)
{
  if (disc < 0 || disc >= int(m_discs.size()))
    return;
  auto& sections = m_discs[disc].m_sections;
  if (section < 0 || section >= int(sections.size()))
    return;
  auto& options = sections[section].m_options;
  if (option < 0 || option >= int(options.size()))
    return;
  if (choice < 0 || choice > int(options[option].m_choices.size()))
    return;
  options[option].m_selected_choice = static_cast<u32>(choice);
  emit changed();
}

void Riivolution::save()
{
  // RiivolutionPatches.saveConfigImpl.
  const auto path = ConfigPath(m_game_id);
  if (!path)
    return;
  DiscIO::Riivolution::Config config;
  for (const auto& disc : m_discs)
  {
    for (const auto& section : disc.m_sections)
    {
      for (const auto& option : section.m_options)
      {
        std::string id = option.m_id.empty() ? (section.m_name + option.m_name) : option.m_id;
        config.m_options.emplace_back(
            DiscIO::Riivolution::ConfigOption{std::move(id), option.m_selected_choice});
      }
    }
  }
  DiscIO::Riivolution::WriteConfigFile(*path, config);
}

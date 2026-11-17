// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/Cheats.h"

#include <algorithm>
#include <set>

#include <QMetaObject>
#include <QStringList>
#include <QVariantMap>

#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/StringUtil.h"
#include "Core/ARDecrypt.h"
#include "Core/ConfigManager.h"
#include "Core/GeckoCodeConfig.h"
#include "DolphinUT/Emulator.h"
#include "VideoCommon/GraphicsModSystem/Config/GraphicsMod.h"
#include "VideoCommon/GraphicsModSystem/Config/GraphicsModGroup.h"

namespace
{
QString Q(const std::string& text)
{
  return QString::fromStdString(text);
}

// The codes as text, one line each (the Android app's getCode).
QString CodeText(const PatchEngine::Patch& patch)
{
  QStringList lines;
  for (const auto& entry : patch.entries)
    lines.append(Q(PatchEngine::SerializeLine(entry)));
  return lines.join(QLatin1Char('\n'));
}

QString CodeText(const ActionReplay::ARCode& code)
{
  QStringList lines;
  for (const auto& op : code.ops)
    lines.append(Q(ActionReplay::SerializeLine(op)));
  return lines.join(QLatin1Char('\n'));
}

QString CodeText(const Gecko::GeckoCode& code)
{
  QStringList lines;
  for (const auto& line : code.codes)
    lines.append(Q(line.original_line));
  return lines.join(QLatin1Char('\n'));
}

QString ErrorOnLine(std::size_t line)
{
  return QStringLiteral("Error on line %1").arg(line + 1);
}

std::string LocalIniPath(const QString& game_id)
{
  return File::GetUserPath(D_GAMESETTINGS_IDX) + game_id.toStdString() + ".ini";
}
}  // namespace

Cheats::Cheats(QObject* parent) : QObject(parent)
{
}

Cheats::~Cheats()
{
  if (m_download_thread.joinable())
    m_download_thread.join();
}

bool Cheats::load(const QString& game_id, int revision, const QString& gametdb_id)
{
  if (game_id.isEmpty() || m_downloading)
    return false;
  m_game_id = game_id;
  m_revision = revision;
  m_gametdb_id = gametdb_id.toStdString();

  // As the JNI's loadCodes: the user's codes always come from GameSettings/<ID>.ini (not
  // LoadLocalGameIni), the shipped ones from the default INIs.
  Common::IniFile local;
  local.Load(LocalIniPath(game_id));
  const Common::IniFile defaults =
      SConfig::LoadDefaultGameIni(game_id.toStdString(), static_cast<u16>(revision));
  m_patches.clear();
  PatchEngine::LoadPatchSection("OnFrame", &m_patches, defaults, local);
  m_ar_codes = ActionReplay::LoadCodes(defaults, local);
  m_gecko_codes = Gecko::LoadCodes(defaults, local);

  // GraphicsModGroup.getMods: only mods with a feature for a target group some mod defines.
  m_mod_group = std::make_unique<GraphicsModGroupConfig>(game_id.toStdString());
  m_mod_group->Load();
  std::set<std::string> groups;
  for (const GraphicsModConfig& mod : m_mod_group->GetMods())
  {
    for (const auto& group : mod.m_groups)
      groups.insert(group.m_name);
  }
  m_mods.clear();
  for (GraphicsModConfig& mod : m_mod_group->GetMods())
  {
    if (std::ranges::any_of(mod.m_features, [&groups](const auto& feature) {
          return groups.contains(feature.m_group);
        }))
    {
      m_mods.push_back(&mod);
    }
  }

  Emulator::LogFromAnyThread(QStringLiteral("cheats %1: %2 patches, %3 AR, %4 Gecko, %5 mods")
                                 .arg(game_id)
                                 .arg(m_patches.size())
                                 .arg(m_ar_codes.size())
                                 .arg(m_gecko_codes.size())
                                 .arg(m_mods.size()));
  emit changed();
  return true;
}

QVariantList Cheats::GetItems() const
{
  QVariantList items;
  const auto add = [&items](const QString& type, int index, const std::string& name,
                            const QString& creator, const QString& notes, const QString& code,
                            bool enabled, bool user_defined) {
    items.append(QVariantMap{
        {QStringLiteral("type"), type},
        {QStringLiteral("index"), index},
        {QStringLiteral("name"), Q(name)},
        {QStringLiteral("creator"), creator},
        {QStringLiteral("notes"), notes},
        {QStringLiteral("code"), code},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("userDefined"), user_defined},
    });
  };
  for (std::size_t i = 0; i < m_patches.size(); ++i)
  {
    const auto& p = m_patches[i];
    add(QStringLiteral("patch"), int(i), p.name, {}, {}, CodeText(p), p.enabled, p.user_defined);
  }
  for (std::size_t i = 0; i < m_ar_codes.size(); ++i)
  {
    const auto& c = m_ar_codes[i];
    add(QStringLiteral("ar"), int(i), c.name, {}, {}, CodeText(c), c.enabled, c.user_defined);
  }
  for (std::size_t i = 0; i < m_gecko_codes.size(); ++i)
  {
    const auto& c = m_gecko_codes[i];
    QStringList notes;
    for (const std::string& line : c.notes)
      notes.append(Q(line));
    add(QStringLiteral("gecko"), int(i), c.name, Q(c.creator), notes.join(QLatin1Char('\n')),
        CodeText(c), c.enabled, c.user_defined);
  }
  for (std::size_t i = 0; i < m_mods.size(); ++i)
  {
    const auto* m = m_mods[i];
    add(QStringLiteral("mod"), int(i), m->m_title, Q(m->m_author), Q(m->m_description), {},
        m->m_enabled, false);
  }
  return items;
}

void Cheats::setEnabled(const QString& type, int index, bool enabled)
{
  if (type == QLatin1String("patch") && index >= 0 && index < int(m_patches.size()))
    m_patches[index].enabled = enabled;
  else if (type == QLatin1String("ar") && index >= 0 && index < int(m_ar_codes.size()))
    m_ar_codes[index].enabled = enabled;
  else if (type == QLatin1String("gecko") && index >= 0 && index < int(m_gecko_codes.size()))
    m_gecko_codes[index].enabled = enabled;
  else if (type == QLatin1String("mod") && index >= 0 && index < int(m_mods.size()))
    m_mods[index]->m_enabled = enabled;
  else
    return;
  Save(type);
  emit changed();
}

QString Cheats::add(const QString& type, const QString& name, const QString& creator,
                    const QString& notes, const QString& code)
{
  return Apply(type, -1, name, creator, notes, code);
}

QString Cheats::edit(const QString& type, int index, const QString& name, const QString& creator,
                     const QString& notes, const QString& code)
{
  return Apply(type, index, name, creator, notes, code);
}

// The JNI's setCheatImpl for each type, and the Android app's checks (CheatDetailsFragment):
// a name, at least one line, each line valid; AR lines all encrypted or all decrypted (encrypted
// ones are decrypted). index -1 adds a user-defined code; otherwise only user-defined codes change.
QString Cheats::Apply(const QString& type, int index, const QString& name, const QString& creator,
                      const QString& notes, const QString& code)
{
  // Codes shipped with Dolphin cannot be changed (CheatDetailsFragment shows no Edit for them).
  const auto editable = [index](const auto& list) {
    return index < 0 || (index < int(list.size()) && list[index].user_defined);
  };
  if ((type == QLatin1String("patch") && !editable(m_patches)) ||
      (type == QLatin1String("ar") && !editable(m_ar_codes)) ||
      (type == QLatin1String("gecko") && !editable(m_gecko_codes)))
  {
    return QStringLiteral("Only codes you added can be edited.");
  }
  if (name.trimmed().isEmpty())
    return QStringLiteral("Name can't be empty");
  const std::vector<std::string> lines = SplitString(code.toStdString(), '\n');

  if (type == QLatin1String("patch"))
  {
    std::vector<PatchEngine::PatchEntry> entries;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
      if (lines[i].empty())
        continue;
      auto entry = PatchEngine::DeserializeLine(lines[i]);
      if (!entry)
        return ErrorOnLine(i);
      entries.push_back(*entry);
    }
    if (entries.empty())
      return QStringLiteral("Code can't be empty");
    PatchEngine::Patch& patch = index < 0 ? m_patches.emplace_back() : m_patches[index];
    patch.user_defined = true;
    patch.name = name.toStdString();
    patch.entries = std::move(entries);
  }
  else if (type == QLatin1String("ar"))
  {
    std::vector<ActionReplay::AREntry> entries;
    std::vector<std::string> encrypted;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
      if (lines[i].empty())
        continue;
      auto parsed = ActionReplay::DeserializeLine(lines[i]);
      if (auto* entry = std::get_if<ActionReplay::AREntry>(&parsed))
        entries.push_back(*entry);
      else if (auto* line = std::get_if<ActionReplay::EncryptedLine>(&parsed))
        encrypted.push_back(*line);
      else
        return ErrorOnLine(i);
    }
    if (!encrypted.empty())
    {
      if (!entries.empty())
        return QStringLiteral("Lines must either be all encrypted or all decrypted");
      ActionReplay::DecryptARCode(encrypted, &entries);
    }
    if (entries.empty())
      return QStringLiteral("Code can't be empty");
    ActionReplay::ARCode& ar = index < 0 ? m_ar_codes.emplace_back() : m_ar_codes[index];
    ar.user_defined = true;
    ar.name = name.toStdString();
    ar.ops = std::move(entries);
  }
  else if (type == QLatin1String("gecko"))
  {
    std::vector<Gecko::GeckoCode::Code> entries;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
      if (lines[i].empty())
        continue;
      auto entry = Gecko::DeserializeLine(lines[i]);
      if (!entry)
        return ErrorOnLine(i);
      entries.push_back(*entry);
    }
    if (entries.empty())
      return QStringLiteral("Code can't be empty");
    Gecko::GeckoCode& gecko = index < 0 ? m_gecko_codes.emplace_back() : m_gecko_codes[index];
    gecko.user_defined = true;
    gecko.name = name.toStdString();
    gecko.creator = creator.toStdString();
    gecko.notes = SplitString(notes.toStdString(), '\n');
    gecko.codes = std::move(entries);
  }
  else
  {
    return QStringLiteral("Unknown code type.");
  }
  Save(type);
  emit changed();
  return {};
}

void Cheats::remove(const QString& type, int index)
{
  // CheatDetailsFragment offers Delete for user-defined codes only.
  const auto erase = [index](auto& list) {
    if (index < 0 || index >= int(list.size()) || !list[index].user_defined)
      return false;
    list.erase(list.begin() + index);
    return true;
  };
  bool removed = false;
  if (type == QLatin1String("patch"))
    removed = erase(m_patches);
  else if (type == QLatin1String("ar"))
    removed = erase(m_ar_codes);
  else if (type == QLatin1String("gecko"))
    removed = erase(m_gecko_codes);
  if (!removed)
    return;
  Save(type);
  emit changed();
}

void Cheats::Save(const QString& type)
{
  if (m_game_id.isEmpty())
    return;
  if (type == QLatin1String("mod"))
  {
    if (m_mod_group)
      m_mod_group->Save();
    return;
  }
  // The JNI's saveCodes: GameSettings/<ID>.ini, read, changed and written back.
  const std::string path = LocalIniPath(m_game_id);
  Common::IniFile local;
  local.Load(path);
  if (type == QLatin1String("patch"))
    PatchEngine::SavePatchSection(&local, m_patches);
  else if (type == QLatin1String("ar"))
    ActionReplay::SaveCodes(&local, m_ar_codes);
  else if (type == QLatin1String("gecko"))
    Gecko::SaveCodes(local, m_gecko_codes);
  if (!local.Save(path))
    Emulator::LogFromAnyThread(QStringLiteral("cheats: could not write %1").arg(Q(path)));
}

bool Cheats::downloadGecko()
{
  if (m_downloading || m_gametdb_id.empty())
    return false;
  if (m_download_thread.joinable())
    m_download_thread.join();
  m_downloading = true;
  emit downloadingChanged();
  m_download_thread = std::thread([this, id = m_gametdb_id] {
    auto result = Gecko::DownloadCodes(id);
    QMetaObject::invokeMethod(this, [this, result = std::move(result)] {
      m_downloading = false;
      emit downloadingChanged();
      if (!result)
      {
        Emulator::LogFromAnyThread(QStringLiteral("cheats: Gecko download failed"));
        emit downloadFinished(-1, 0);
        return;
      }
      // CheatsViewModel.addDownloadedGeckoCodes: only codes not already there.
      int added = 0;
      for (const Gecko::GeckoCode& code : *result)
      {
        if (std::ranges::find(m_gecko_codes, code) == m_gecko_codes.end())
        {
          m_gecko_codes.push_back(code);
          ++added;
        }
      }
      if (added > 0)
      {
        Save(QStringLiteral("gecko"));
        emit changed();
      }
      Emulator::LogFromAnyThread(QStringLiteral("cheats: downloaded %1 Gecko codes, added %2")
                                     .arg(result->size())
                                     .arg(added));
      emit downloadFinished(int(result->size()), added);
    });
  });
  return true;
}

// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/GameList.h"

#include <algorithm>
#include <span>
#include <string_view>

#include <QImage>
#include <QMetaObject>
#include <QUrl>

#include "Common/Config/Config.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/UISettings.h"
#include "DiscIO/Blob.h"
#include "DiscIO/Enums.h"
#include "DolphinUT/Emulator.h"
#include "UICommon/UICommon.h"

namespace
{
// The Android app keeps this in its own section ("Android"); the port keeps its own settings in
// "UbuntuTouch".
const Config::Info<bool> UT_SHOW_GAME_TITLES{{Config::System::Main, "UbuntuTouch", "ShowGameTitles"},
                                             true};

GameListModel::PlatformTab TabOf(DiscIO::Platform platform)
{
  switch (platform)
  {
  case DiscIO::Platform::GameCubeDisc:
  case DiscIO::Platform::Triforce:
    return GameListModel::GameCubeTab;
  case DiscIO::Platform::WiiDisc:
    return GameListModel::WiiTab;
  default:
    return GameListModel::WiiWareTab;
  }
}

QString ImageKey(const UICommon::GameFile& game)
{
  // The cache replaces a GameFile with an updated copy when its metadata changes, so the address
  // changes with it.
  return QString::number(reinterpret_cast<quintptr>(&game), 16);
}

QString ToQString(const std::string& text)
{
  return QString::fromStdString(text);
}
}  // namespace

// Lives on GameListModel::m_thread and owns the cache; everything here runs on that thread.
class GameScanner : public QObject
{
public:
  explicit GameScanner(GameListModel* model) : m_model(model) {}

  void Load()
  {
    m_cache.Load();
    m_cache.ForEach([this](const std::shared_ptr<const UICommon::GameFile>& game) { Added(game); });
  }

  void Scan(const std::vector<std::string>& folders, bool recursive)
  {
    m_model->m_scan_queued = false;

    std::vector<std::string_view> folder_views(folders.begin(), folders.end());
    const std::vector<std::string> paths = UICommon::FindAllGamePaths(folder_views, recursive);
    Emulator::LogFromAnyThread(QStringLiteral("game list: %1 files in %2 folders")
                                   .arg(paths.size())
                                   .arg(folders.size()));

    const auto added = [this](const std::shared_ptr<const UICommon::GameFile>& game) {
      Added(game);
    };
    const auto removed = [this](const std::string& path) {
      QMetaObject::invokeMethod(m_model, [model = m_model, path] { model->RemoveGame(path); },
                                Qt::QueuedConnection);
    };

    bool changed = m_cache.Update(paths, added, removed, m_halt);
    // Reads banners and titles, and downloads covers from GameTDB when that is enabled.
    changed |= m_cache.UpdateAdditionalMetadata(added, m_halt);
    if (changed)
      m_cache.Save();

    QMetaObject::invokeMethod(m_model, [model = m_model] { model->ScanFinished(); },
                              Qt::QueuedConnection);
  }

  std::atomic<bool> m_halt = false;

private:
  void Added(const std::shared_ptr<const UICommon::GameFile>& game)
  {
    QMetaObject::invokeMethod(
        m_model, [model = m_model, game] { model->AddOrUpdateGame(game); }, Qt::QueuedConnection);
  }

  GameListModel* m_model;
  UICommon::GameFileCache m_cache;
};

GameListModel::GameListModel(QObject* parent)
    : QAbstractListModel(parent), m_scanner(new GameScanner(this))
{
  m_thread.setObjectName(QStringLiteral("Game list"));
  m_scanner->moveToThread(&m_thread);
  m_thread.start();

  m_loading = true;
  QMetaObject::invokeMethod(m_scanner, [scanner = m_scanner] { scanner->Load(); });
  refresh();
}

GameListModel::~GameListModel()
{
  m_scanner->m_halt = true;
  m_thread.quit();
  m_thread.wait();
  delete m_scanner;
}

int GameListModel::rowCount(const QModelIndex& parent) const
{
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant GameListModel::data(const QModelIndex& index, int role) const
{
  if (!index.isValid() || index.row() >= rowCount())
    return {};
  const UICommon::GameFile& game = *m_games[index.row()];
  switch (role)
  {
  case Qt::DisplayRole:
  case TitleRole:
    return TitleOf(game);
  case PathRole:
    return ToQString(game.GetFilePath());
  case CompanyRole:
    return ToQString(DiscIO::GetCompanyFromID(game.GetMakerID()));
  case GameIdRole:
    return ToQString(game.GetGameID());
  case PlatformTabRole:
    return static_cast<int>(TabOf(game.GetPlatform()));
  case CoverRole:
    return GetUseCovers() && !game.GetCoverImage().empty() ? GameImageProvider::CoverUrl(game) :
                                                             QString();
  default:
    return {};
  }
}

QHash<int, QByteArray> GameListModel::roleNames() const
{
  return {
      {PathRole, "path"},       {TitleRole, "title"},
      {CompanyRole, "company"}, {GameIdRole, "gameId"},
      {PlatformTabRole, "platformTab"}, {CoverRole, "cover"},
  };
}

QString GameListModel::TitleOf(const UICommon::GameFile& game) const
{
  return ToQString(game.GetName(m_title_database));
}

void GameListModel::refresh()
{
  if (!m_loading)
  {
    m_loading = true;
    emit loadingChanged();
  }
  if (m_scan_queued.exchange(true))
    return;
  // Config is read here, on the UI thread, as DolphinQt does.
  QMetaObject::invokeMethod(m_scanner, [scanner = m_scanner, folders = Config::GetIsoPaths(),
                                        recursive = GetSearchSubfolders()] {
    scanner->Scan(folders, recursive);
  });
}

int GameListModel::IndexOf(const std::string& path) const
{
  const auto it = std::ranges::find_if(
      m_games, [&path](const auto& game) { return game->GetFilePath() == path; });
  return it == m_games.end() ? -1 : static_cast<int>(it - m_games.begin());
}

void GameListModel::AddOrUpdateGame(std::shared_ptr<const UICommon::GameFile> game)
{
  const int index = IndexOf(game->GetFilePath());
  if (index >= 0)
  {
    {
      std::lock_guard lock(m_games_mutex);
      m_games[index] = std::move(game);
    }
    emit dataChanged(this->index(index), this->index(index));
    return;
  }

  const int row = rowCount();
  beginInsertRows({}, row, row);
  {
    std::lock_guard lock(m_games_mutex);
    m_games.push_back(std::move(game));
  }
  endInsertRows();
  emit countChanged();
}

void GameListModel::RemoveGame(const std::string& path)
{
  const int index = IndexOf(path);
  if (index < 0)
    return;
  beginRemoveRows({}, index, index);
  {
    std::lock_guard lock(m_games_mutex);
    m_games.erase(m_games.begin() + index);
  }
  endRemoveRows();
  emit countChanged();
}

void GameListModel::ScanFinished()
{
  // Another scan may have been queued meanwhile; it reports when it ends.
  if (m_scan_queued)
    return;
  m_loading = false;
  emit loadingChanged();
  Emulator::LogFromAnyThread(QStringLiteral("game list: %1 games").arg(rowCount()));
}

std::shared_ptr<const UICommon::GameFile> GameListModel::FindGame(const QString& path) const
{
  const std::string path_string = path.toStdString();
  std::lock_guard lock(m_games_mutex);
  for (const auto& game : m_games)
  {
    if (game->GetFilePath() == path_string)
      return game;
  }
  return nullptr;
}

QVariantMap GameListModel::details(const QString& path) const
{
  const auto game = FindGame(path);
  if (!game)
    return {};

  const QString file_size = ToQString(UICommon::FormatSize(game->GetFileSize()));
  QVariantMap details{
      {QStringLiteral("title"), TitleOf(*game)},
      {QStringLiteral("description"),
       ToQString(game->GetDescription(UICommon::GameFile::Variant::LongAndPossiblyCustom))},
      {QStringLiteral("country"), ToQString(DiscIO::GetName(game->GetCountry(), true))},
      {QStringLiteral("company"), ToQString(DiscIO::GetCompanyFromID(game->GetMakerID()))},
      {QStringLiteral("gameId"), ToQString(game->GetGameID())},
      {QStringLiteral("revision"), static_cast<int>(game->GetRevision())},
      {QStringLiteral("banner"),
       game->GetBannerImage().empty() ? QString() : GameImageProvider::BannerUrl(*game)},
      {QStringLiteral("path"), path},
      // A Wii disc can update the Wii's system software (stage 6.3, the Android app's
      // "Perform System Update" in the game properties).
      {QStringLiteral("wiiDisc"), game->GetPlatform() == DiscIO::Platform::WiiDisc},
      // Stage 6.4, the Android app's game properties: Riivolution and Set as Default ISO for discs,
      // Convert File where Dolphin allows it, and the IDs the cheats need.
      {QStringLiteral("disc"), game->GetPlatform() == DiscIO::Platform::GameCubeDisc ||
                                   game->GetPlatform() == DiscIO::Platform::Triforce ||
                                   game->GetPlatform() == DiscIO::Platform::WiiDisc},
      {QStringLiteral("convertible"), game->ShouldAllowConversion()},
      {QStringLiteral("gametdbId"), ToQString(game->GetGameTDBID())},
      {QStringLiteral("discNumber"), static_cast<int>(game->GetDiscNumber())},
  };

  // As the Android app: the file format, compression and block size only where they mean something.
  if (!game->ShouldShowFileFormatDetails())
  {
    details[QStringLiteral("fileSize")] = file_size;
  }
  else
  {
    details[QStringLiteral("fileFormat")] =
        QStringLiteral("%1 (%2)").arg(ToQString(game->GetFileFormatName()), file_size);
    const std::string& compression = game->GetCompressionMethod();
    details[QStringLiteral("compression")] =
        compression.empty() ? QStringLiteral("No Compression") : ToQString(compression);
    if (game->GetBlockSize() > 0)
      details[QStringLiteral("blockSize")] = ToQString(UICommon::FormatSize(game->GetBlockSize(), 0));
  }
  return details;
}

QStringList GameListModel::GetFolders() const
{
  QStringList folders;
  for (const std::string& folder : Config::GetIsoPaths())
    folders.append(ToQString(folder));
  return folders;
}

void GameListModel::addFolder(const QString& folder)
{
  std::vector<std::string> folders = Config::GetIsoPaths();
  const std::string new_folder = folder.toStdString();
  if (new_folder.empty() || std::ranges::find(folders, new_folder) != folders.end())
    return;
  folders.push_back(new_folder);
  Config::SetIsoPaths(folders);
  Config::Save();
  emit foldersChanged();
  refresh();
}

void GameListModel::removeFolder(const QString& folder)
{
  std::vector<std::string> folders = Config::GetIsoPaths();
  std::erase(folders, folder.toStdString());
  Config::SetIsoPaths(folders);
  Config::Save();
  emit foldersChanged();
  refresh();
}

bool GameListModel::GetSearchSubfolders() const
{
  return Config::Get(Config::MAIN_RECURSIVE_ISO_PATHS);
}

void GameListModel::SetSearchSubfolders(bool search)
{
  if (search == GetSearchSubfolders())
    return;
  Config::SetBase(Config::MAIN_RECURSIVE_ISO_PATHS, search);
  Config::Save();
  emit foldersChanged();
  refresh();
}

bool GameListModel::GetUseCovers() const
{
  return Config::Get(Config::MAIN_USE_GAME_COVERS);
}

void GameListModel::SetUseCovers(bool use)
{
  if (use == GetUseCovers())
    return;
  Config::SetBase(Config::MAIN_USE_GAME_COVERS, use);
  Config::Save();
  emit optionsChanged();
  if (rowCount() > 0)
    emit dataChanged(index(0), index(rowCount() - 1), {CoverRole});
  // Downloads the covers that are missing.
  if (use)
    refresh();
}

bool GameListModel::GetShowTitles() const
{
  return Config::Get(UT_SHOW_GAME_TITLES);
}

void GameListModel::SetShowTitles(bool show)
{
  if (show == GetShowTitles())
    return;
  Config::SetBase(UT_SHOW_GAME_TITLES, show);
  Config::Save();
  emit optionsChanged();
}

GameListFilter::GameListFilter(QObject* parent) : QSortFilterProxyModel(parent)
{
  setSortRole(GameListModel::TitleRole);
  setSortCaseSensitivity(Qt::CaseInsensitive);
  setDynamicSortFilter(true);
  sort(0);
  connect(this, &QAbstractItemModel::rowsInserted, this, &GameListFilter::countChanged);
  connect(this, &QAbstractItemModel::rowsRemoved, this, &GameListFilter::countChanged);
  connect(this, &QAbstractItemModel::modelReset, this, &GameListFilter::countChanged);
  connect(this, &QAbstractItemModel::layoutChanged, this, &GameListFilter::countChanged);
}

void GameListFilter::SetGames(QObject* games)
{
  setSourceModel(qobject_cast<QAbstractItemModel*>(games));
  sort(0);
  emit changed();
  emit countChanged();
}

void GameListFilter::SetPlatformTab(int tab)
{
  if (tab == m_platform_tab)
    return;
  beginFilterChange();
  m_platform_tab = tab;
  endFilterChange(QSortFilterProxyModel::Direction::Rows);
  emit changed();
  emit countChanged();
}

void GameListFilter::SetSearchText(const QString& text)
{
  if (text == m_search_text)
    return;
  beginFilterChange();
  m_search_text = text;
  endFilterChange(QSortFilterProxyModel::Direction::Rows);
  emit changed();
  emit countChanged();
}

bool GameListFilter::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
  const QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
  if (m_platform_tab >= 0 && index.data(GameListModel::PlatformTabRole).toInt() != m_platform_tab)
    return false;
  if (m_search_text.isEmpty())
    return true;
  return index.data(GameListModel::TitleRole).toString().contains(m_search_text, Qt::CaseInsensitive) ||
         index.data(GameListModel::GameIdRole).toString().contains(m_search_text, Qt::CaseInsensitive);
}

bool GameListFilter::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
  const int order = QString::localeAwareCompare(left.data(GameListModel::TitleRole).toString(),
                                                right.data(GameListModel::TitleRole).toString());
  if (order != 0)
    return order < 0;
  return left.data(GameListModel::PathRole).toString() < right.data(GameListModel::PathRole).toString();
}

GameImageProvider::GameImageProvider(const GameListModel* games)
    : QQuickImageProvider(QQuickImageProvider::Image), m_games(games)
{
}

QString GameImageProvider::CoverUrl(const UICommon::GameFile& game)
{
  return QStringLiteral("image://dolphin/cover/%1/%2")
      .arg(ImageKey(game), QString::fromLatin1(QUrl::toPercentEncoding(ToQString(game.GetFilePath()))));
}

QString GameImageProvider::BannerUrl(const UICommon::GameFile& game)
{
  return QStringLiteral("image://dolphin/banner/%1/%2")
      .arg(ImageKey(game), QString::fromLatin1(QUrl::toPercentEncoding(ToQString(game.GetFilePath()))));
}

QImage GameImageProvider::requestImage(const QString& id, QSize* size, const QSize& requested_size)
{
  // <kind>/<key>/<percent-encoded path>
  const QString kind = id.section(QLatin1Char('/'), 0, 0);
  const QString path = QUrl::fromPercentEncoding(id.section(QLatin1Char('/'), 2).toLatin1());
  const auto game = m_games->FindGame(path);

  QImage image;
  if (game && kind == QLatin1String("cover"))
  {
    const std::vector<u8>& buffer = game->GetCoverImage().buffer;
    image = QImage::fromData(buffer.data(), static_cast<int>(buffer.size()));
  }
  else if (game && kind == QLatin1String("banner"))
  {
    const UICommon::GameBanner& banner = game->GetBannerImage();
    if (!banner.empty())
    {
      image = QImage(reinterpret_cast<const uchar*>(banner.buffer.data()),
                     static_cast<int>(banner.width), static_cast<int>(banner.height),
                     QImage::Format_ARGB32)
                  .copy();
    }
  }

  // QML's sourceSize may give only one side (the other 0).
  if (!image.isNull() && requested_size.width() > 0 && requested_size.height() > 0)
    image = image.scaled(requested_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  else if (!image.isNull() && requested_size.width() > 0)
    image = image.scaledToWidth(requested_size.width(), Qt::SmoothTransformation);
  else if (!image.isNull() && requested_size.height() > 0)
    image = image.scaledToHeight(requested_size.height(), Qt::SmoothTransformation);
  if (size)
    *size = image.size();
  return image;
}

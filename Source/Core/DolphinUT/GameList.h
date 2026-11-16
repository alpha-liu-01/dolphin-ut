// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QAbstractListModel>
#include <QQuickImageProvider>
#include <QSortFilterProxyModel>
#include <QThread>
#include <QVariantMap>

#include "Core/TitleDatabase.h"
#include "UICommon/GameFile.h"
#include "UICommon/GameFileCache.h"

class GameScanner;

// The game library (stage 3.1): the games in the configured folders, from Dolphin's game file
// cache, as the Android app's GameFileCacheManager and DolphinQt's GameTracker do. Scanning,
// reading new files and downloading covers run on a worker thread.
class GameListModel : public QAbstractListModel
{
  Q_OBJECT
  Q_PROPERTY(bool loading READ IsLoading NOTIFY loadingChanged)
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
  Q_PROPERTY(QStringList folders READ GetFolders NOTIFY foldersChanged)
  Q_PROPERTY(bool searchSubfolders READ GetSearchSubfolders WRITE SetSearchSubfolders NOTIFY
                 foldersChanged)
  Q_PROPERTY(bool useCovers READ GetUseCovers WRITE SetUseCovers NOTIFY optionsChanged)
  Q_PROPERTY(bool showTitles READ GetShowTitles WRITE SetShowTitles NOTIFY optionsChanged)

public:
  // The Android app's tabs: GameCube (with Triforce), Wii, WiiWare (with ELF/DOL files).
  enum PlatformTab
  {
    GameCubeTab = 0,
    WiiTab = 1,
    WiiWareTab = 2,
  };
  Q_ENUM(PlatformTab)

  enum Role
  {
    PathRole = Qt::UserRole + 1,
    TitleRole,
    CompanyRole,
    GameIdRole,
    PlatformTabRole,
    CoverRole,  // an image:// URL, or empty when the game has no cover
  };

  explicit GameListModel(QObject* parent = nullptr);
  ~GameListModel() override;

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  // Rescans the folders. Several requests while a scan runs end up as one more scan.
  Q_INVOKABLE void refresh();
  // The Android app's game details dialog, as a map for QML.
  Q_INVOKABLE QVariantMap details(const QString& path) const;
  Q_INVOKABLE void addFolder(const QString& folder);
  Q_INVOKABLE void removeFolder(const QString& folder);

  // Any thread (the image provider).
  std::shared_ptr<const UICommon::GameFile> FindGame(const QString& path) const;

  bool IsLoading() const { return m_loading; }
  QStringList GetFolders() const;
  bool GetSearchSubfolders() const;
  void SetSearchSubfolders(bool search);
  bool GetUseCovers() const;
  void SetUseCovers(bool use);
  bool GetShowTitles() const;
  void SetShowTitles(bool show);

signals:
  void loadingChanged();
  void countChanged();
  void foldersChanged();
  void optionsChanged();

private:
  friend class GameScanner;

  // UI thread, from the scanner.
  void AddOrUpdateGame(std::shared_ptr<const UICommon::GameFile> game);
  void RemoveGame(const std::string& path);
  void ScanFinished();

  int IndexOf(const std::string& path) const;
  QString TitleOf(const UICommon::GameFile& game) const;

  std::vector<std::shared_ptr<const UICommon::GameFile>> m_games;
  mutable std::mutex m_games_mutex;  // for FindGame from other threads; writes are on the UI thread
  Core::TitleDatabase m_title_database;

  QThread m_thread;
  GameScanner* m_scanner;
  std::atomic<bool> m_scan_queued = false;
  bool m_loading = false;
};

// One platform tab of the library, sorted by title, optionally filtered by a search text.
class GameListFilter : public QSortFilterProxyModel
{
  Q_OBJECT
  Q_PROPERTY(QObject* games READ GetGames WRITE SetGames NOTIFY changed)
  Q_PROPERTY(int platformTab READ GetPlatformTab WRITE SetPlatformTab NOTIFY changed)
  Q_PROPERTY(QString searchText READ GetSearchText WRITE SetSearchText NOTIFY changed)
  Q_PROPERTY(int count READ GetCount NOTIFY countChanged)

public:
  explicit GameListFilter(QObject* parent = nullptr);

  QObject* GetGames() const { return sourceModel(); }
  void SetGames(QObject* games);
  int GetPlatformTab() const { return m_platform_tab; }
  void SetPlatformTab(int tab);
  QString GetSearchText() const { return m_search_text; }
  void SetSearchText(const QString& text);
  int GetCount() const { return rowCount(); }

signals:
  void changed();
  void countChanged();

protected:
  bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
  bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
  int m_platform_tab = -1;  // -1: all
  QString m_search_text;
};

// image://dolphin/cover/<key>/<path> and image://dolphin/banner/<key>/<path>, where <path> is
// percent-encoded and <key> changes when the game's metadata does (so QML reloads the image).
class GameImageProvider : public QQuickImageProvider
{
public:
  explicit GameImageProvider(const GameListModel* games);

  QImage requestImage(const QString& id, QSize* size, const QSize& requested_size) override;

  static QString CoverUrl(const UICommon::GameFile& game);
  static QString BannerUrl(const UICommon::GameFile& game);

private:
  const GameListModel* m_games;
};

// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <thread>

#include <QObject>
#include <QString>
#include <QVariantMap>

// Convert File (stage 6.4, docs/ut-port/stage6.4-game-tools.md), as the Android app's
// ConvertFragment and NativeLibrary.ConvertDiscImage: ISO, GCZ, WIA or RVZ, with Dolphin's own
// DiscIO converters on a worker thread, progress and cancel. The output is deleted on failure or
// cancel by the converters themselves.
class DiscConverter : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool busy READ IsBusy NOTIFY busyChanged)
  Q_PROPERTY(QString progressText READ GetProgressText NOTIFY progressChanged)
  Q_PROPERTY(double progress READ GetProgress NOTIFY progressChanged)

public:
  explicit DiscConverter(QObject* parent = nullptr);
  ~DiscConverter() override;

  bool IsBusy() const { return m_busy; }
  QString GetProgressText() const { return m_progress_text; }
  double GetProgress() const { return m_progress; }

  // {title, gameId, blobType (DiscIO::BlobType number), wii, nkit, datel, allowed, folder, baseName}
  // for a game file in the library.
  Q_INVOKABLE QVariantMap info(const QString& path) const;
  // format: DiscIO::BlobType (0 ISO, 3 GCZ, 7 WIA, 8 RVZ); compression: WIARVZCompressionType.
  Q_INVOKABLE bool convert(const QString& in_path, const QString& out_path, int format,
                           int block_size, int compression, int compression_level, bool scrub);
  Q_INVOKABLE void cancel() { m_cancel = true; }

signals:
  void busyChanged();
  void progressChanged();
  // cancelled: no message is shown (the Android app shows none).
  void finished(bool ok, bool cancelled, const QString& out_path);

private:
  bool m_busy = false;
  QString m_progress_text;
  double m_progress = 0;
  std::atomic<bool> m_cancel = false;
  std::thread m_thread;
};

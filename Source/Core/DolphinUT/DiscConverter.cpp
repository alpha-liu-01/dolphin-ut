// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinUT/DiscConverter.h"

#include <memory>

#include <QFileInfo>
#include <QMetaObject>

#include "Core/Core.h"
#include "Core/System.h"
#include "DiscIO/Blob.h"
#include "DiscIO/Enums.h"
#include "DiscIO/ScrubbedBlob.h"
#include "DiscIO/WIABlob.h"
#include "DolphinUT/Emulator.h"
#include "UICommon/GameFile.h"

DiscConverter::DiscConverter(QObject* parent) : QObject(parent)
{
}

DiscConverter::~DiscConverter()
{
  m_cancel = true;
  if (m_thread.joinable())
    m_thread.join();
}

QVariantMap DiscConverter::info(const QString& path) const
{
  const UICommon::GameFile game(path.toStdString());
  const QFileInfo file(path);
  return {
      {QStringLiteral("title"),
       QString::fromStdString(game.GetName(UICommon::GameFile::Variant::LongAndPossiblyCustom))},
      {QStringLiteral("gameId"), QString::fromStdString(game.GetGameID())},
      {QStringLiteral("blobType"), static_cast<int>(game.GetBlobType())},
      {QStringLiteral("wii"), game.GetPlatform() == DiscIO::Platform::WiiDisc},
      {QStringLiteral("nkit"), game.IsNKit()},
      {QStringLiteral("datel"), game.IsDatelDisc()},
      {QStringLiteral("allowed"), game.IsValid() && game.ShouldAllowConversion()},
      {QStringLiteral("folder"), file.absolutePath()},
      {QStringLiteral("baseName"), file.completeBaseName()},
  };
}

bool DiscConverter::convert(const QString& in_path, const QString& out_path, int format,
                            int block_size, int compression, int compression_level, bool scrub)
{
  if (m_busy || !Core::IsUninitialized(Core::System::GetInstance()) ||
      QFileInfo(in_path).absoluteFilePath() == QFileInfo(out_path).absoluteFilePath())
  {
    return false;
  }
  if (m_thread.joinable())
    m_thread.join();
  const UICommon::GameFile game(in_path.toStdString());
  const bool wii = game.GetPlatform() == DiscIO::Platform::WiiDisc;

  m_busy = true;
  m_cancel = false;
  m_progress = 0;
  m_progress_text.clear();
  emit busyChanged();
  emit progressChanged();
  Emulator::LogFromAnyThread(QStringLiteral("convert %1 → %2 (format %3, block %4, compression %5 "
                                            "level %6, scrub %7)")
                                 .arg(in_path, out_path)
                                 .arg(format)
                                 .arg(block_size)
                                 .arg(compression)
                                 .arg(compression_level)
                                 .arg(scrub));

  m_thread = std::thread([this, in = in_path.toStdString(), out = out_path.toStdString(), format,
                          block_size, compression, compression_level, scrub, wii, out_path] {
    // NativeLibrary.ConvertDiscImage.
    std::unique_ptr<DiscIO::BlobReader> reader =
        scrub ? DiscIO::ScrubbedBlob::Create(in) : DiscIO::CreateBlobReader(in);
    const auto callback = [this](const std::string& text, float completion) {
      QMetaObject::invokeMethod(this, [this, text = QString::fromStdString(text), completion] {
        m_progress_text = text;
        m_progress = completion;
        emit progressChanged();
      });
      return !m_cancel.load();
    };
    bool ok = false;
    if (reader)
    {
      switch (static_cast<DiscIO::BlobType>(format))
      {
      case DiscIO::BlobType::PLAIN:
        ok = DiscIO::ConvertToPlain(reader.get(), in, out, callback);
        break;
      case DiscIO::BlobType::GCZ:
        ok = DiscIO::ConvertToGCZ(reader.get(), in, out, wii ? 1 : 0, block_size, callback);
        break;
      case DiscIO::BlobType::WIA:
      case DiscIO::BlobType::RVZ:
        ok = DiscIO::ConvertToWIAOrRVZ(reader.get(), in, out,
                                       static_cast<DiscIO::BlobType>(format) == DiscIO::BlobType::RVZ,
                                       static_cast<DiscIO::WIARVZCompressionType>(compression),
                                       compression_level, block_size, callback);
        break;
      default:
        break;
      }
    }
    const bool cancelled = m_cancel.load();
    QMetaObject::invokeMethod(this, [this, ok, cancelled, out_path] {
      Emulator::LogFromAnyThread(QStringLiteral("convert: %1")
                                     .arg(cancelled ? QStringLiteral("cancelled") :
                                          ok        ? QStringLiteral("done") :
                                                      QStringLiteral("failed")));
      m_busy = false;
      emit busyChanged();
      emit finished(ok && !cancelled, cancelled, out_path);
    });
  });
  return true;
}

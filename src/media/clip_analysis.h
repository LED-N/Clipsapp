#pragma once
#include <QImage>
#include <QList>
#include <QVector>
#include <QString>
#include <atomic>
#include <optional>
namespace clipsapp {
struct ClipAnalysis { QVector<QImage> thumbnails; QList<QVector<float>> peaks; QString error; QList<int> streams; };
ClipAnalysis analyzeClip(const QString& path, const std::atomic_bool& stop, std::optional<QList<int>> audioStreams = std::nullopt);
}

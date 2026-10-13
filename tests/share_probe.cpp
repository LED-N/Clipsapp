#include "media/clip_reader.h"
#include "media/clip_exporter.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
using namespace clipsapp;
namespace { QByteArray hashFile(const QString& path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(&file); return hash.result(); } }
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"input", "Local original, opened read-only", "file"}); parser.addOption({"output", "New directory for the share copy and numerical report", "directory"});
    parser.addOption({"codec", "Native GPU codec h264/hevc", "codec", "h264"}); parser.addOption({"max-mb", "Maximum decimal MB", "MB", "20"}); parser.process(app);
    if (!parser.isSet("input") || !parser.isSet("output")) return 2;
    const auto directory = QDir(parser.value("output")).absolutePath(); if (!QDir().mkpath(directory)) return 2;
    const auto input = QFileInfo(parser.value("input")).absoluteFilePath(); const auto before = hashFile(input); if (before.isEmpty()) return 2;
    QString error; QStringList stages; const auto destination = QDir(directory).filePath("result.discord.mp4"); bool completed = false;
    QElapsedTimer elapsed; elapsed.start();
    try {
        ClipReader reader(input, false, true); const auto info = reader.info(); ClipExporter exporter; QEventLoop loop; QTimer timeout; timeout.setSingleShot(true); timeout.start(300000);
        QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { error = "Timeout"; exporter.cancel(); });
        QObject::connect(&exporter, &ClipExporter::stageChanged, &loop, [&](const QString& stage) { stages.append(stage); });
        QObject::connect(&exporter, &ClipExporter::completed, &loop, [&](const QString&) { completed = true; loop.quit(); });
        QObject::connect(&exporter, &ClipExporter::failed, &loop, [&](const QString& value) { error = value; loop.quit(); });
        QObject::connect(&exporter, &ClipExporter::cancelled, &loop, &QEventLoop::quit);
        ClipExportOptions options; options.maxBytes = qRound64(parser.value("max-mb").toDouble() * 1000000); options.codec = parser.value("codec");
        if (exporter.start(info, defaultEdit(info), destination, error, {options.codec + "_nvenc"}, options)) loop.exec();
    } catch (const std::exception& caught) { error = QString::fromUtf8(caught.what()); }
    const auto after = hashFile(input); QJsonObject report{{"completed", completed}, {"sourceUnchanged", before == after}, {"maxBytes", qRound64(parser.value("max-mb").toDouble() * 1000000)}, {"sizeBytes", QFileInfo(destination).size()}, {"elapsedMs", elapsed.elapsed()}, {"error", error}, {"stages", QJsonArray::fromStringList(stages)}};
    if (completed) { ClipReader reader(destination, false, true); report["width"] = reader.info().size.width(); report["height"] = reader.info().size.height(); report["fps"] = reader.info().fps; report["durationMs"] = reader.info().durationMs; report["audioTracks"] = reader.info().audio.size(); }
    const bool passed = completed && before == after && QFileInfo(destination).size() <= report["maxBytes"].toInteger(); report["passed"] = passed;
    QFile file(QDir(directory).filePath("report.json")); if (!file.open(QIODevice::WriteOnly)) return 2; file.write(QJsonDocument(report).toJson(QJsonDocument::Indented)); return passed ? 0 : 1;
}

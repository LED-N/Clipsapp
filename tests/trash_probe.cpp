#include "core/clip_library.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <thread>

using namespace clipsapp;
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"output", "Directory for the numerical report and generated fixture", "directory"}); parser.process(app);
    if (!parser.isSet("output")) return 2;
    const auto output = QDir(parser.value("output")).absolutePath(); if (!QDir().mkpath(output)) return 2;
    QTemporaryDir directory(QDir(output).filePath("generated-XXXXXX")); if (!directory.isValid()) return 2;
    const auto path = directory.filePath("clipsapp-generated-trash-test.mp4");
    QFile fixture(path); if (!fixture.open(QIODevice::WriteOnly)) return 2;
    fixture.write("Clipsapp native trash test: generated placeholder, no video or audio."); fixture.close();
    ClipTrashResult result; std::thread worker([&] { result = trashClips({path}); }); worker.join();
    const bool passed = result.removed == QStringList{path} && result.errors.isEmpty() && !QFile::exists(path);
    QJsonObject report{{"passed", passed}, {"fixtureOnly", true}, {"workerThread", true}, {"errors", result.errors.join('\n')}};
    QFile file(QDir(output).filePath("report.json")); if (!file.open(QIODevice::WriteOnly)) return 2;
    file.write(QJsonDocument(report).toJson(QJsonDocument::Indented)); return passed ? 0 : 1;
}

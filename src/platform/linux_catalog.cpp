#include "source_catalog.h"
#include <QGuiApplication>
#include <QScreen>
#include <QProcess>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace clipsapp {
SourceCatalog SourceCatalog::discover() {
    SourceCatalog catalog;
    const bool wayland = QGuiApplication::platformName().contains("wayland");
    if (wayland) {
        catalog.videoTargets.append({"portal:choose", QStringLiteral("Choisir l'écran ou la fenêtre au démarrage")});
        catalog.notes << QStringLiteral("La sélection de l'image se fera dans la fenêtre de partage du système.");
    } else {
        for (const auto* screen : QGuiApplication::screens()) {
            const auto size = screen->size();
            catalog.videoTargets.append({"screen:" + screen->name(), QStringLiteral("Écran %1 · %2 × %3").arg(screen->name()).arg(size.width()).arg(size.height())});
        }
    }
    QProcess process;
    process.start("pw-dump", QStringList{});
    if (!process.waitForStarted(500) || !process.waitForFinished(2000)) {
        process.kill(); process.waitForFinished(500);
        catalog.notes << QStringLiteral("La liste audio nécessite PipeWire et la commande pw-dump.");
        return catalog;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(process.readAllStandardOutput(), &error);
    if (process.exitCode() != 0 || error.error != QJsonParseError::NoError || !document.isArray()) {
        catalog.notes << QStringLiteral("Les flux audio PipeWire ne sont pas accessibles."); return catalog;
    }
    QSet<QString> applications;
    for (const auto& value : document.array()) {
        const auto object = value.toObject();
        if (object.value("type").toString() != "PipeWire:Interface:Node") continue;
        const auto props = object.value("info").toObject().value("props").toObject();
        const QString mediaClass = props.value("media.class").toString();
        const QString nodeName = props.value("node.name").toString();
        QString label = props.value("node.description").toString(props.value("node.nick").toString(nodeName));
        if (mediaClass == "Audio/Source" && !nodeName.isEmpty()) catalog.microphones.append({nodeName, label});
        if (mediaClass == "Audio/Sink" && !nodeName.isEmpty()) catalog.outputs.append({nodeName, label});
        if (mediaClass == "Stream/Output/Audio") {
            const QString appId = props.value("application.id").toString();
            const QString binary = props.value("application.process.binary").toString();
            const QString name = props.value("application.name").toString();
            const QString identity = !appId.isEmpty() ? "app:" + appId : (!binary.isEmpty() ? "binary:" + binary : "name:" + name);
            if (identity == "name:" || applications.contains(identity)) continue;
            applications.insert(identity);
            catalog.applications.append({identity, name.isEmpty() ? binary : name});
        }
    }
    return catalog;
}
}

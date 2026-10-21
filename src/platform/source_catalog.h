#pragma once
#include <QList>
#include <QString>
#include "core/audio_routing.h"
#include "core/config.h"

namespace clipsapp {
struct SourceTarget { QString id; QString label; };
struct SourceCatalog {
    QList<SourceTarget> microphones;
    QList<SourceTarget> applications;
    // Every program open now, playing sound or not. A browser closes its audio process when it
    // falls silent: it is still there for a source that listens to it.
    QList<SourceTarget> runningApplications;
    QList<SourceTarget> outputs;
    QList<SourceTarget> videoTargets;
    QStringList notes;
    static SourceCatalog discover();
    const QList<SourceTarget>& audioTargets(SourceKind kind) const;
    // The application open now that a saved path designates, even from before an update; none if it is closed.
    SourceTarget runningApplication(const QString& path) const;
};
inline const QList<SourceTarget>& SourceCatalog::audioTargets(SourceKind kind) const {
    if (kind == SourceKind::Microphone) return microphones;
    if (kind == SourceKind::Application || kind == SourceKind::ApplicationMix) return applications;
    return outputs;
}
inline SourceTarget SourceCatalog::runningApplication(const QString& path) const {
    const auto identity = applicationIdentity(path);
    for (const auto& list : {&applications, &runningApplications}) for (const auto& target : *list) if (applicationIdentity(target.id) == identity) return target;
    return {};
}
}


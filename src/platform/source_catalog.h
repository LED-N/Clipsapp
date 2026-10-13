#pragma once
#include <QList>
#include <QString>
#include "core/config.h"

namespace clipsapp {
struct SourceTarget { QString id; QString label; };
struct SourceCatalog {
    QList<SourceTarget> microphones;
    QList<SourceTarget> applications;
    QList<SourceTarget> outputs;
    QList<SourceTarget> videoTargets;
    QStringList notes;
    static SourceCatalog discover();
    const QList<SourceTarget>& audioTargets(SourceKind kind) const;
};
inline const QList<SourceTarget>& SourceCatalog::audioTargets(SourceKind kind) const {
    if (kind == SourceKind::Microphone) return microphones;
    if (kind == SourceKind::Application || kind == SourceKind::ApplicationMix) return applications;
    return outputs;
}
}


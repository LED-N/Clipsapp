#pragma once
#include <QString>
#include <QStringList>
class QWidget;
namespace clipsapp {
// The answer to a deletion: nothing, the trash of the system, or gone for good.
enum class Deletion { Kept, Trash, Permanent };
// Asked each time, never remembered: the trash stays the way unless its box is ticked on purpose.
// "copies" names them as copies; "note" says, above the rest, why they go (an original that a
// compressed version replaces, say).
Deletion confirmTrash(QWidget* parent, const QStringList& paths, bool copies, const QString& note = {});
}

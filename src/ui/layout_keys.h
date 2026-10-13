#pragma once
#include <QString>
#include <Qt>
class QKeyEvent;
namespace clipsapp {
// The image-by-image keys are chosen for where they sit, not for their letter: the two keys
// on either side of the left hand's "forward" key. That is A and E on an AZERTY keyboard and
// Q and E on a QWERTY one; the names shown follow the keyboard in use.
// Every other shortcut of the application is a key without a letter, or a letter chosen for
// what it stands for (F, I, O, K, M, Ctrl + C, Ctrl + F); all but M sit at the same place on both.
struct StepKeys {
    Qt::Key back = Qt::Key_Q, forward = Qt::Key_E;
    QString backLabel = QStringLiteral("Q"), forwardLabel = QStringLiteral("E");
};
// What a keyboard layout puts on one of these two keys: its virtual key and the character it types.
struct LayoutKey { quint32 virtualKey = 0, character = 0; };
StepKeys stepKeysFor(LayoutKey back, LayoutKey forward);
StepKeys stepKeys();
// -1 for the key that steps back one image, 1 for the one that steps forward, 0 for any other.
int frameStep(const QKeyEvent* event);
}

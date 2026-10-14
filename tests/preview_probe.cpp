#include "ui/clip_editor.h"
#include "ui/clip_viewer.h"
#include "ui/theme.h"
#include "ui/video_canvas.h"
#include "ui/video_surface.h"
#include "ui/widgets.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QTimer>
#include <algorithm>
#include <cstring>
#include <windows.h>
#include <vector>
using namespace clipsapp;

// Manual hardware check of what a user sees of a clip: it plays one in a real window, the editor or
// the full-screen viewer, and measures how many images per second reach the screen. The window is
// pushed out of the screen, two pixels aside: Windows draws nothing for a window it does not show
// at all. --screenshot keeps the window as Windows composes it; --visible leaves it where it can be seen.
namespace {
class Paints final : public QObject {
public:
    std::vector<double> gaps; QElapsedTimer since; bool counting = false; int paints = 0; qint64 shown = 0; const VideoCanvas* canvas = nullptr;
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (counting && event->type() == QEvent::Paint) {
            ++paints; if (canvas->frameKey() == shown) return false;
            shown = canvas->frameKey(); if (since.isValid()) gaps.push_back(since.nsecsElapsed() / 1e6); since.start();
        }
        return false;
    }
};
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); applyTheme(app);
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"input", "Local clip to play", "file"});
    parser.addOption({"output-dir", "Directory for the report", "directory"});
    parser.addOption({"size", "Size of the window", "widthxheight", "1900x1000"});
    parser.addOption({"viewer", "Play in the full-screen viewer, kept to that size, instead of the editor"});
    parser.addOption({"visible", "Leave the window on the screen"});
    parser.addOption({"grab", "Save what the view holds at the end, as a picture", "file"});
    parser.addOption({"screenshot", "Save the window as Windows composes it", "file"});
    parser.addOption({"crop", "Editor: crop the clip to its central half"});
    parser.addOption({"result", "Editor: show the result of the crop instead of the crop tool"});
    parser.addOption({"still", "Editor: do not play, keep the first image"});
    parser.addOption({"stress", "Editor: seek, pause, change view and size, and open the clip again, all the while"});
    parser.addOption({"seconds", "Playback duration to observe", "seconds", "6"}); parser.process(app);
    if (!parser.isSet("input") || !parser.isSet("output-dir")) return 2;
    const QDir directory(parser.value("output-dir")); if (!QDir().mkpath(directory.path())) return 2;
    const auto dimensions = parser.value("size").split('x'); if (dimensions.size() != 2) return 2;
    const QSize size(dimensions[0].toInt(), dimensions[1].toInt()); const int seconds = std::clamp(parser.value("seconds").toInt(), 2, 60);
    const auto screen = QGuiApplication::primaryScreen()->geometry();
    const QPoint corner = parser.isSet("visible") ? screen.topLeft() + QPoint(40, 40) : QPoint(screen.right() - 1, screen.bottom() - 1);
    // Neither window takes the keyboard from whatever is being used meanwhile.
    QWidget owner; owner.setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus); owner.setAttribute(Qt::WA_ShowWithoutActivating); owner.resize(4, 4); owner.move(corner);
    ClipEditor editor; ClipViewer viewer(&owner); const bool full = parser.isSet("viewer"); QWidget* window = full ? static_cast<QWidget*>(&viewer) : &editor;
    window->setWindowFlags((full ? Qt::Window : Qt::Tool) | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus); window->setAttribute(Qt::WA_ShowWithoutActivating);
    window->resize(size); window->move(corner);
    auto* canvas = editor.findChild<VideoCanvas*>(); auto* play = editor.findChild<QPushButton*>("play-pause"); if (!canvas || !play) return 2;
    Paints paints; paints.canvas = canvas; canvas->installEventFilter(&paints);
    if (full) { owner.show(); viewer.setProperty("windowed", true); viewer.setVolume(0); viewer.open({parser.value("input")}, 0); viewer.move(corner); }
    else { editor.show(); editor.openClip(parser.value("input")); }
    const auto surface = [&]() -> VideoSurface* { return full ? viewer.findChild<VideoSurface*>() : canvas->surface(); };
    QElapsedTimer waiting; waiting.start(); bool started = false; QElapsedTimer playing; quint64 handed = 0; qint64 lastKey = 0; int images = 0, step = -1; double cpuStart = 0;
    // Processor time used by the whole program, in seconds: one core kept busy gives one per second.
    const auto cpuSeconds = [] { FILETIME created, exited, kernel, user; GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user); const auto ticks = [](const FILETIME& time) { return (quint64(time.dwHighDateTime) << 32 | time.dwLowDateTime) / 1e7; }; return ticks(kernel) + ticks(user); };
    QTimer tick; tick.setInterval(2); tick.setTimerType(Qt::PreciseTimer);
    QObject::connect(&tick, &QTimer::timeout, &app, [&] {
        if (!started) {
            if (waiting.elapsed() > 15000) app.exit(3);
            if (full) { if (viewer.index() < 0 || (viewer.image().isNull() && !(surface() && surface()->isVisible()))) return; }
            else {
                if (!editor.ready() || !play->isEnabled()) return;
                if (parser.isSet("crop")) {
                    const auto frame = editor.clipInfo().size; const QRect half(frame.width() / 4 & ~1, frame.height() / 4 & ~1, frame.width() / 2 & ~1, frame.height() / 2 & ~1);
                    editor.findChild<QSpinBox*>("crop-width")->setValue(half.width()); editor.findChild<QSpinBox*>("crop-height")->setValue(half.height());
                    editor.findChild<QSpinBox*>("crop-x")->setValue(half.x()); editor.findChild<QSpinBox*>("crop-y")->setValue(half.y());
                }
                if (parser.isSet("result")) editor.findChild<Segmented*>("crop-mode")->setCurrentIndex(1);
                if (!parser.isSet("still")) play->click();
            }
            started = true; playing.start(); paints.counting = true; if (surface()) handed = surface()->presented(); cpuStart = cpuSeconds(); return;
        }
        // The viewer's images from memory are counted as they change; those of the card by the surface itself.
        if (full && viewer.image().cacheKey() != lastKey) { lastKey = viewer.image().cacheKey(); ++images; }
        // Everything a hurried user does at once: nothing may freeze, and the picture must keep coming.
        if (parser.isSet("stress") && !full && playing.elapsed() / 60 != step) {
            step = int(playing.elapsed() / 60); const auto duration = std::max<qint64>(1, editor.clipInfo().durationMs);
            switch (step % 7) {
            case 0: editor.showPosition((step * 977) % duration); break;
            case 1: if (editor.ready()) play->click(); break;
            case 2: if (auto* mode = editor.findChild<Segmented*>("crop-mode")) mode->setCurrentIndex(step / 7 % 2); break;
            case 3: editor.resize(size - QSize(step % 5 * 40, step % 3 * 30)); break;
            case 4: if (auto* width = editor.findChild<QSpinBox*>("crop-width"); width && editor.ready()) width->setValue(editor.clipInfo().size.width() / (1 + step % 3) & ~1); break;
            case 5: if (step % 35 == 5) editor.openClip(parser.value("input")); break;
            default: editor.showPosition(duration - 1 - (step * 131) % duration); break;
            }
        }
        if (playing.elapsed() < seconds * 1000) return;
        tick.stop(); paints.counting = false; auto gaps = paints.gaps; std::sort(gaps.begin(), gaps.end());
        auto* shown = surface(); const bool direct = shown && shown->isVisible(); const double elapsed = playing.elapsed() / 1000.0;
        if (parser.isSet("screenshot")) {
            const QSize pixels = window->size() * window->devicePixelRatioF(); QImage picture(pixels, QImage::Format_RGB32); picture.fill(Qt::magenta);
            BITMAPINFO format{}; format.bmiHeader.biSize = sizeof(format.bmiHeader); format.bmiHeader.biWidth = pixels.width(); format.bmiHeader.biHeight = -pixels.height(); format.bmiHeader.biPlanes = 1; format.bmiHeader.biBitCount = 32;
            const HWND handle = reinterpret_cast<HWND>(window->winId()); const HDC screenContext = GetDC(nullptr), memory = CreateCompatibleDC(screenContext); void* bits = nullptr;
            const HBITMAP bitmap = CreateDIBSection(memory, &format, DIB_RGB_COLORS, &bits, nullptr, 0); const auto previous = SelectObject(memory, bitmap);
            if (PrintWindow(handle, memory, 2 /* PW_RENDERFULLCONTENT */) && bits) std::memcpy(picture.bits(), bits, size_t(picture.sizeInBytes()));
            SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screenContext); picture.save(parser.value("screenshot"));
        }
        if (parser.isSet("grab")) (direct ? shown->grabFrame() : full ? viewer.image() : canvas->grab().toImage()).save(parser.value("grab"));
        const auto at = [&](double share) { return gaps.empty() ? 0.0 : gaps[std::min(gaps.size() - 1, size_t(gaps.size() * share))]; };
        const QJsonObject report{{"fps", full ? 0.0 : editor.clipInfo().fps}, {"direct", direct}, {"coresBusy", (cpuSeconds() - cpuStart) / elapsed}, {"imagesPerSecond", direct ? (shown->presented() - handed) / elapsed : full ? images / elapsed : gaps.size() / elapsed},
            {"gapMedianMs", at(0.5)}, {"gapP95Ms", at(0.95)}, {"gapMaxMs", gaps.empty() ? 0 : gaps.back()}};
        QFile file(directory.filePath("report.json")); const bool written = file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(report).toJson()) > 0;
        if (full) viewer.finish(); else editor.suspend();
        app.exit(written ? 0 : 2);
    });
    tick.start(); return app.exec();
}

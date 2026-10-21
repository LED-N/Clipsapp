// The images of the README: the real interface, over a library of generated clips, laid out beside
// a few words in the style of the banner. Run by hand: clipsapp_readme_images --output .github
#include "ui/main_window.h"
#include "ui/clip_list.h"
#include "ui/replay_strip.h"
#include "ui/settings_page.h"
#include "ui/theme.h"
#include "ui/theme_editor.h"
#include "core/config_store.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
using namespace clipsapp;
namespace {
// Rendered at twice the size, then reduced: the text stays sharp at any scale.
constexpr double renderScale = 2.0;
void wait(int ms) { QElapsedTimer timer; timer.start(); while (timer.elapsed() < ms) { QApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(5); } }
struct Clip { QString game, name; int seconds; unsigned from, to; int daysAgo; QString time; };
// A clip of gradients with three tracks of sound, like a clip of Clipsapp.
bool makeClip(const QString& ffmpeg, const QString& path, const Clip& clip) {
    const auto color = [](unsigned value) { return QStringLiteral("0x%1").arg(value, 6, 16, QLatin1Char('0')); };
    const auto voice = [&clip](double pitch, double pace) { return QStringLiteral("aevalsrc=exprs='0.45*sin(2*PI*%1*t)*abs(sin(%2*t))*(0.4+0.6*abs(sin(0.37*t)))':s=48000:d=%3").arg(pitch).arg(pace).arg(clip.seconds); };
    const QStringList arguments{"-y", "-loglevel", "error",
        "-f", "lavfi", "-i", QStringLiteral("gradients=s=1920x1080:r=30:c0=%1:c1=%2:speed=0.012:d=%3").arg(color(clip.from), color(clip.to)).arg(clip.seconds),
        "-f", "lavfi", "-i", voice(180, 1.3), "-f", "lavfi", "-i", voice(330, 0.6), "-f", "lavfi", "-i", voice(260, 2.1),
        "-map", "0:v", "-map", "1:a", "-map", "2:a", "-map", "3:a", "-c:v", "libopenh264", "-b:v", "6000k", "-c:a", "aac", "-b:a", "96k",
        "-metadata:s:a:0", "handler_name=Micro", "-metadata:s:a:1", "handler_name=Jeu", "-metadata:s:a:2", "handler_name=Discord", path};
    QProcess process; process.start(ffmpeg, arguments); process.waitForFinished(120000);
    if (process.exitCode() != 0) { qWarning("ffmpeg : %s", process.readAllStandardError().constData()); return false; }
    QFile file(path); file.open(QIODevice::ReadWrite);
    const auto when = QDateTime(QDate::currentDate().addDays(-clip.daysAgo), QTime::fromString(clip.time, QStringLiteral("HH:mm")));
    file.setFileTime(when, QFileDevice::FileModificationTime); return true;
}
void makeScreenshot(const QString& path, unsigned from, unsigned to) {
    QImage image(1920, 1080, QImage::Format_RGB32); QPainter painter(&image);
    QLinearGradient gradient(0, 0, 1920, 1080); gradient.setColorAt(0, tk::rgb(from)); gradient.setColorAt(1, tk::rgb(to)); painter.fillRect(image.rect(), gradient); painter.end();
    QDir().mkpath(QFileInfo(path).absolutePath()); image.save(path);
}
// A frame of the window, reduced to the scale of the image.
QImage frame(QWidget& window, double scale) {
    // Drawn at twice its size by the widgets themselves: text and lines stay sharp once reduced.
    QImage shot(window.size() * renderScale, QImage::Format_ARGB32_Premultiplied); shot.setDevicePixelRatio(renderScale); shot.fill(tk::window); window.render(&shot);
    shot.setDevicePixelRatio(1);
    return shot.scaled(QSize(qRound(window.width() * scale), qRound(window.height() * scale)), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
// The dark ground of the banner, a little light coming from the top.
QImage ground(int width, int height) {
    QImage image(width, height, QImage::Format_RGB32); QPainter painter(&image); painter.fillRect(image.rect(), tk::window);
    QRadialGradient glow(QPointF(width * 0.62, -height * 0.25), width * 0.55); glow.setColorAt(0, tk::mix(tk::window, tk::rail, 1.0).lighter(135)); glow.setColorAt(1, tk::window);
    painter.fillRect(image.rect(), glow); return image;
}
// The window as a card: its top left corner rounded, the rest leaving the image.
void placeWindow(QPainter& painter, const QImage& window, QPointF at, double radius = 26) {
    painter.save(); painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape; shape.addRoundedRect(QRectF(at, window.size()), radius, radius);
    painter.setClipPath(shape); painter.drawImage(at, window); painter.setClipping(false);
    painter.setPen(QPen(tk::lineStrong, 2)); painter.setBrush(Qt::NoBrush); painter.drawPath(shape); painter.restore();
}
QFont font(int pixels, int weight, bool display = false) {
    QFont result; result.setFamilies({display ? QStringLiteral("Segoe UI Variable Display") : QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")});
    result.setPixelSize(pixels); result.setWeight(QFont::Weight(weight)); return result;
}
// Text with words in the accent: "Vos meilleurs moments, *enregistrés* et *rangés*".
double paragraph(QPainter& painter, const QString& text, QRectF box, const QFont& face, const QColor& color, double lineHeight) {
    painter.setFont(face); const QFontMetricsF metrics(face); double x = box.left(), y = box.top() + metrics.ascent(); bool accent = false;
    for (const auto& word : text.split(' ')) {
        QString shown = word; bool starts = shown.startsWith('*'), ends = shown.endsWith('*') || shown.endsWith(QStringLiteral("*,")) || shown.endsWith(QStringLiteral("*."));
        if (starts) { accent = true; shown.remove(0, 1); }
        QString tail; if (ends) { const int star = shown.lastIndexOf('*'); tail = shown.mid(star + 1); shown = shown.left(star); }
        const double width = metrics.horizontalAdvance(shown + tail), space = metrics.horizontalAdvance(' ');
        if (x + width > box.right() && x > box.left()) { x = box.left(); y += lineHeight; }
        painter.setPen(accent ? tk::accent : color); painter.drawText(QPointF(x, y), shown); x += metrics.horizontalAdvance(shown);
        if (ends) accent = false;
        painter.setPen(color); painter.drawText(QPointF(x, y), tail); x += metrics.horizontalAdvance(tail) + space;
    }
    return y - metrics.ascent() + lineHeight;
}
void chips(QPainter& painter, const QStringList& texts, QPointF at) {
    const auto face = font(30, QFont::DemiBold); painter.setFont(face); const QFontMetricsF metrics(face); double x = at.x();
    for (const auto& text : texts) {
        const QRectF box(x, at.y(), metrics.horizontalAdvance(text) + 40, 58);
        painter.setPen(QPen(tk::accentLine, 1.5)); painter.setBrush(tk::liveBg); painter.drawRoundedRect(box, 8, 8);
        painter.setPen(tk::accent); painter.drawText(box, Qt::AlignCenter, text); x = box.right() + 18;
    }
}
// The words beside a feature: what it is, in a few words, and three things it does.
void pitch(QPainter& painter, QRectF box, const QString& eyebrow, const QString& heading, const QString& text, const QStringList& points) {
    painter.setRenderHint(QPainter::Antialiasing); painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(font(26, QFont::Bold)); painter.setPen(tk::accent); painter.drawText(QPointF(box.left(), box.top() + 26), eyebrow);
    double y = paragraph(painter, heading, QRectF(box.left(), box.top() + 54, box.width(), 400), font(68, QFont::Bold, true), tk::text, 80);
    y = paragraph(painter, text, QRectF(box.left(), y + 36, box.width(), 400), font(32, QFont::Normal), tk::text2, 46);
    y += 26;
    for (const auto& point : points) {
        painter.setPen(Qt::NoPen); painter.setBrush(tk::accentSoft); painter.drawEllipse(QRectF(box.left(), y + 4, 34, 34));
        paintIcon(painter, QStringLiteral("check"), QRectF(box.left() + 7, y + 11, 20, 20), tk::accent);
        paragraph(painter, point, QRectF(box.left() + 54, y, box.width() - 54, 200), font(30, QFont::DemiBold), tk::text, 42); y += 56;
    }
}
ReplayView liveView(bool recording) {
    ReplayView view; view.state = ReplayView::Running; view.replay = true; view.seconds = 60; view.capacity = 60; view.bytes = qint64(214) << 20; view.canSave = true;
    view.hotkey = QStringLiteral("Ctrl+Alt+F10"); view.recordHotkey = QStringLiteral("Ctrl+Alt+F9"); view.liveSummary = QStringLiteral("Nova Strike détecté · 1440p 60 FPS · h264_nvenc");
    view.recording = recording; view.recordingSeconds = 42 * 60 + 17; view.recordingBytes = qint64(3400) << 20; return view;
}
void showStrip(MainWindow& window, const ReplayView& view) { auto* strip = window.findChild<ReplayStrip*>(); strip->apply(view); strip->layout()->activate(); }
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); applyTheme(app);
    QCommandLineParser parser; parser.addHelpOption(); parser.addOption({"output", "Folder of the images", "directory"}); parser.addOption({"work", "Folder of the generated library", "directory"}); parser.process(app);
    if (!parser.isSet("output") || !parser.isSet("work")) return 2;
    const QDir output(parser.value("output")), work(parser.value("work")); output.mkpath("."); work.mkpath(".");
    const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg")); if (ffmpeg.isEmpty()) { qWarning("ffmpeg introuvable dans le PATH"); return 2; }

    // A library as a player has one: two games, clips of today and yesterday, screenshots.
    const auto library = work.filePath("Clipsapp"); QDir(library).removeRecursively();
    const QList<Clip> clips{
        {"Nova Strike", "Clutch 1v3", 42, 0x1E8C7E, 0x0E3B4A, 0, "21:42"}, {"Turbo Ball", "Arrêt décisif", 60, 0x5B3FD9, 0x2A1460, 0, "20:15"},
        {"Nova Strike", "Défuse au buzzer", 18, 0x2F6FDB, 0x101C40, 0, "19:03"}, {"Nova Strike", "Ace en défense", 30, 0xD9652E, 0x5A1E2E, 1, "23:10"},
        {"Turbo Ball", "But en ciseau", 25, 0xD4A13A, 0x5A2A0E, 1, "22:31"}, {"Turbo Ball", "Remontée finale", 48, 0xC0408A, 0x2A1240, 1, "21:05"}};
    QString first;
    for (const auto& clip : clips) {
        const auto path = QStringLiteral("%1/%2/%3.mp4").arg(library, clip.game, clip.name); QDir().mkpath(QFileInfo(path).absolutePath());
        if (!makeClip(ffmpeg, path, clip)) return 3;
        if (first.isEmpty()) first = path;
    }
    for (int index = 0; index < 4; ++index) makeScreenshot(QStringLiteral("%1/Screenshots/Nova Strike/Nova Strike-%2.png").arg(library).arg(index), 0x1E8C7E + index * 0x101010, 0x0E1B2A);
    const auto configDirectory = work.filePath("config"); QDir(configDirectory).removeRecursively();
    { auto config = AppConfig::defaults(); config.libraryPath = library; config.profiles[0].video.width = 2560; config.profiles[0].video.height = 1440; config.profiles[0].video.bitrateMbps = 50; QString error; if (!ConfigStore(configDirectory).save(config, error)) return 2; }

    MainWindow window(configDirectory); window.setAttribute(Qt::WA_DontShowOnScreen); window.resize(1220, 820); window.show();
    auto* list = window.findChild<ClipListView*>("clip-library");
    for (int tries = 0; tries < 400; ++tries) { wait(50); if (list && list->model() && list->model()->rowCount() >= int(clips.size())) break; }
    wait(2500); QThreadPool::globalInstance()->waitForDone(20000); wait(500);
    const double scale = 1.2; // The window at 1220 px, as it opens, fills the right of the image.

    // The banner: the library, the replay and a recording side by side.
    {
        window.setPage(MainWindow::Library); showStrip(window, liveView(true));
        auto image = ground(2560, 960); QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        paintLogo(painter, QRectF(120, 210, 118, 118));
        painter.setFont(font(132, QFont::Bold, true)); painter.setPen(tk::text); painter.drawText(QPointF(266, 318), QStringLiteral("Clipsapp"));
        double y = paragraph(painter, QStringLiteral("Vos meilleurs moments, *enregistrés* et *rangés* sur votre PC."), QRectF(120, 380, 940, 300), font(54, QFont::DemiBold, true), tk::text, 66);
        y = paragraph(painter, QStringLiteral("Replay, enregistrement, screenshots, bibliothèque, éditeur et partage Discord dans une seule application légère."), QRectF(120, y + 24, 940, 300), font(34, QFont::Normal), tk::text2, 48);
        chips(painter, {QStringLiteral("Sans compte"), QStringLiteral("Sans publicité"), QStringLiteral("Sans télémétrie")}, QPointF(120, y + 40));
        placeWindow(painter, frame(window, scale), QPointF(1110, 110)); painter.end(); image.save(output.filePath("banner.png"));
    }
    // Replay and recording: the capture settings, the two bars above.
    {
        window.setPage(MainWindow::Settings); window.findChild<SettingsPage*>()->showTab(0); showStrip(window, liveView(true)); wait(200); showStrip(window, liveView(true));
        auto image = ground(2560, 1040); QPainter painter(&image); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        pitch(painter, QRectF(120, 150, 900, 800), QStringLiteral("REPLAY ET ENREGISTREMENT"), QStringLiteral("Le moment d'avant, ou la partie entière."),
            QStringLiteral("Le replay garde les dernières secondes, prêtes à devenir un clip. L'enregistrement filme tout, aussi longtemps que vous voulez."),
            {QStringLiteral("Jusqu'en 4K, de 30 à 144 images par seconde"), QStringLiteral("Micro, jeu et Discord sur des pistes séparées"), QStringLiteral("Buffer en mémoire ou sur le disque")});
        placeWindow(painter, frame(window, scale), QPointF(1110, 110)); painter.end(); image.save(output.filePath("readme-capture.png"));
    }
    // Themes: three windows, each in its colors, the editor of the themes in front.
    {
        auto image = ground(2560, 1100); QPainter painter(&image); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const auto presets = themePresets(); QList<QImage> shots;
        const struct { int preset; MainWindow::Page page; } looks[]{{3, MainWindow::Library}, {2, MainWindow::Library}, {1, MainWindow::Settings}};
        for (const auto& look : looks) {
            applyColors(presets[look.preset].colors); window.setPage(look.page);
            if (look.page == MainWindow::Settings) {
                // The editor of the themes shows the colors in use, without making them a change to save.
                window.findChild<SettingsPage*>()->showTab(2); window.findChild<ThemeEditor*>()->setTheme(presets[look.preset].colors); wait(150);
                if (auto* editor = window.findChild<QWidget*>("theme-editor")) for (auto* area : window.findChildren<QScrollArea*>()) if (area->isVisible() && area->widget()->isAncestorOf(editor)) area->verticalScrollBar()->setValue(0);
            }
            showStrip(window, liveView(false)); wait(250); showStrip(window, liveView(false)); shots.append(frame(window, 0.78));
        }
        applyColors(ThemeColors{});
        pitch(painter, QRectF(120, 170, 820, 800), QStringLiteral("THÈMES"), QStringLiteral("Vos couleurs, votre Clipsapp."),
            QStringLiteral("Cinq thèmes pour commencer, ou le vôtre : fond, couleur principale, texte et dégradés, appliqués en direct."),
            {QStringLiteral("Clipsapp, Violet, Bleu nuit, Ambre, Rose"), QStringLiteral("Un code pour partager votre thème"), QStringLiteral("Le logo suit, jusque dans la barre des tâches")});
        placeWindow(painter, shots[0], QPointF(1080, 70), 22); placeWindow(painter, shots[1], QPointF(1330, 230), 22); placeWindow(painter, shots[2], QPointF(1580, 390), 22);
        painter.end(); image.save(output.filePath("readme-themes.png"));
    }
    // The library and the editor: a clip opened, its tracks below the image.
    {
        window.setPage(MainWindow::Library); emit list->openRequested(first);
        for (int tries = 0; tries < 400; ++tries) { wait(50); if (auto* width = window.findChild<QWidget*>("crop-width"); width && width->isEnabled()) break; }
        wait(3000); showStrip(window, liveView(false)); wait(200); showStrip(window, liveView(false));
        auto image = ground(2560, 1040); QPainter painter(&image); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        pitch(painter, QRectF(120, 150, 900, 800), QStringLiteral("BIBLIOTHÈQUE ET ÉDITEUR"), QStringLiteral("Retrouver, couper, partager."),
            QStringLiteral("Vos clips sont rangés par jeu et par jour. L'éditeur découpe, recadre et règle chaque piste ; l'original reste intact."),
            {QStringLiteral("Une copie prête pour Discord, sous la limite de taille"), QStringLiteral("Lecture en plein écran"), QStringLiteral("Screenshots recadrés et annotés")});
        placeWindow(painter, frame(window, scale), QPointF(1110, 110)); painter.end(); image.save(output.filePath("readme-editor.png"));
    }
    window.close(); QThreadPool::globalInstance()->waitForDone(20000);
    return 0;
}

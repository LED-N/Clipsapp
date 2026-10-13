#include "game_identity.h"
#include <QFileInfo>
#include <QRegularExpression>
namespace clipsapp {
QString safeGameFolder(QString name) {
    name.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"), "_");
    name = name.left(80).trimmed(); while (name.endsWith('.') || name.endsWith(' ')) name.chop(1);
    if (name.isEmpty() || name == "." || name == "..") name = "Clips";
    if (QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)", QRegularExpression::CaseInsensitiveOption).match(name).hasMatch()) name.prepend('_');
    return name;
}
QString gameNameFromApplication(const QString& executable, const QString& title, const QString& product) {
    const auto path = QString(executable).replace('\\', '/'); const auto file = QFileInfo(path).completeBaseName();
    const QStringList ignored{"clipsapp", "explorer", "discord", "chrome", "firefox", "msedge", "brave", "opera", "vlc", "steam", "steamwebhelper", "epicgameslauncher", "battle.net", "obs64", "devenv", "code", "applicationframehost", "searchhost", "shellexperiencehost", "taskmgr", "windowsterminal", "powershell", "cmd", "dwm"};
    if (ignored.contains(file.toCaseFolded()) || file.isEmpty()) return {};
    const auto match = QRegularExpression("/steamapps/common/([^/]+)/", QRegularExpression::CaseInsensitiveOption).match(path);
    if (match.hasMatch()) return match.captured(1).trimmed().left(80);
    auto name = product.trimmed();
    if (name.isEmpty() || name.contains("Unreal", Qt::CaseInsensitive) || name.startsWith("UE4", Qt::CaseInsensitive) || name.startsWith("UE5", Qt::CaseInsensitive)) name = title.trimmed();
    if (name.isEmpty()) name = file;
    name.remove(QRegularExpression("\\s*[-·]\\s*(?:DirectX|DX|Vulkan).*?$", QRegularExpression::CaseInsensitiveOption));
    return name.trimmed().left(80);
}
}

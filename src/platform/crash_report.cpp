#include "crash_report.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtGlobal>
#include <exception>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#include <atomic>
#include <csignal>
#include <cstdlib>
#endif

namespace clipsapp {
namespace {
QFileInfoList reports(const QString& directory) { return QDir(directory).entryInfoList({QStringLiteral("crash-*.dmp")}, QDir::Files, QDir::Time); }
#ifdef Q_OS_WIN
// Everything the crash needs is ready beforehand: once the program is broken, nothing is allocated.
wchar_t folder[4096];
std::atomic_bool writing{false};
void appendNumber(wchar_t*& out, unsigned value, int digits) {
    for (int index = digits - 1; index >= 0; --index) { out[index] = wchar_t(L'0' + value % 10); value /= 10; }
    out += digits;
}
void writeReport(EXCEPTION_POINTERS* exception) {
    if (writing.exchange(true)) return;
    wchar_t path[4200]; wchar_t* out = path;
    for (const wchar_t* in = folder; *in; ++in) *out++ = *in;
    for (const wchar_t* in = L"\\crash-"; *in; ++in) *out++ = *in;
    SYSTEMTIME time; GetLocalTime(&time);
    appendNumber(out, time.wYear, 4); appendNumber(out, time.wMonth, 2); appendNumber(out, time.wDay, 2); *out++ = L'-';
    appendNumber(out, time.wHour, 2); appendNumber(out, time.wMinute, 2); appendNumber(out, time.wSecond, 2);
    for (const wchar_t* in = L".dmp"; *in; ++in) *out++ = *in;
    *out = 0;
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION where{GetCurrentThreadId(), exception, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MINIDUMP_TYPE(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules), exception ? &where : nullptr, nullptr, nullptr);
    CloseHandle(file);
}
LONG WINAPI onCrash(EXCEPTION_POINTERS* exception) { writeReport(exception); return EXCEPTION_EXECUTE_HANDLER; }
void onAbort(int) { writeReport(nullptr); _exit(3); }
#endif
}
void installCrashReports(const QString& directory) {
#ifdef Q_OS_WIN
    if (!QDir().mkpath(directory)) return;
    const auto native = QDir::toNativeSeparators(QDir(directory).absolutePath());
    if (native.size() >= 4000) return;
    native.toWCharArray(folder); folder[native.size()] = 0;
    const auto older = reports(directory); for (int index = 5; index < older.size(); ++index) QFile::remove(older[index].absoluteFilePath());
    SetUnhandledExceptionFilter(onCrash);
    std::signal(SIGABRT, onAbort);
    // An exception that nothing caught says so in the journal before the program ends.
    std::set_terminate([] { qCritical("Arrêt sur une exception non traitée."); std::abort(); });
#else
    Q_UNUSED(directory);
#endif
}
QString unseenCrashReport(const QString& directory) {
    const auto found = reports(directory); if (found.isEmpty()) return {};
    const auto marker = QDir(directory).filePath(QStringLiteral("crash-seen"));
    const QFileInfo seen(marker);
    if (seen.exists() && found.first().lastModified() <= seen.lastModified()) return {};
    // Written now, the marker is dated after every report there is.
    QFile file(marker); if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
    return found.first().absoluteFilePath();
}
}

#pragma once
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <functional>
#ifdef Q_OS_LINUX
#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#endif

// Persistent same-directory staging: never auto-remove after an exchange.
// The journal is durable before exchange, so either inode is recoverable after interruption.
namespace SafeCommit {
inline std::function<void()> beforeCommit; // Deterministic test seam; set only while worker is idle.
inline std::function<void()> afterExchange; // Child-process crash seam, before directory fsync.
inline bool commit(const QString &target, const QByteArray &bytes, const QString &journal,
                   const QByteArray &record, bool replace, QString &retained, QString &error)
{
#ifndef Q_OS_LINUX
    Q_UNUSED(target); Q_UNUSED(bytes); Q_UNUSED(journal); Q_UNUSED(record); Q_UNUSED(replace); Q_UNUSED(retained);
    error = "Safe rename primitives unavailable on this platform"; return false;
#else
    retained = QFileInfo(target).absolutePath() + "/.nextnotes-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".swap";
    auto syncDirectory = [](const QString &p) {
        const int fd = ::open(QFile::encodeName(QFileInfo(p).absolutePath()).constData(), O_RDONLY | O_DIRECTORY);
        if (fd < 0) return false;
        const bool ok = ::fsync(fd) == 0; ::close(fd); return ok;
    };
    QFile staging(retained);
    if (!staging.open(QIODevice::WriteOnly | QIODevice::NewOnly) || staging.write(bytes) != bytes.size()
        || !staging.flush() || ::fsync(staging.handle()) != 0 || !syncDirectory(retained)) {
        error = "Cannot persist staging file"; return false;
    }
    staging.close();
    QFile intent(journal);
    const QByteArray payload = record + "\n" + retained.toUtf8();
    if (!intent.open(QIODevice::WriteOnly | QIODevice::NewOnly) || intent.write(payload) != payload.size()
        || !intent.flush() || ::fsync(intent.handle()) != 0 || !syncDirectory(journal)) {
        error = "Cannot persist commit journal"; return false;
    }
    intent.close();
    if (beforeCommit) beforeCommit();
    const unsigned flags = replace ? 2U /* RENAME_EXCHANGE */ : 1U /* RENAME_NOREPLACE */;
    if (::syscall(SYS_renameat2, AT_FDCWD, QFile::encodeName(retained).constData(),
                  AT_FDCWD, QFile::encodeName(target).constData(), flags) != 0) {
        error = QString::fromLocal8Bit(::strerror(errno)); return false;
    }
    if (afterExchange) afterExchange();
    if (!syncDirectory(target)) { error = "Commit directory sync failed; inspect journal"; return false; }
    return true;
#endif
}
}

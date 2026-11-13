#include "ManagedLibrary.h"
#include "ManagedStore.h"
#include "RepositoryTestHooks.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif
#ifdef Q_OS_LINUX
#include <sys/file.h>
#include <sys/stat.h>
#endif

namespace {
void require(bool condition, const QString &code, const QString &detail = {}) {
    if (!condition) throw ManagedStoreError{code, detail};
}
bool present(const QString &path) { const QFileInfo info(path); return info.exists() || info.isSymLink(); }
void checkedDirectory(const QString &path) {
    require(QFileInfo(path).isDir() && !QFileInfo(path).isSymLink()
            && QFileInfo(path).canonicalFilePath() == path, "unsafePath", path);
}
bool validId(const QString &id) {
    static const QRegularExpression pattern("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    return pattern.match(id).hasMatch();
}
void stage(const QString &name) {
    if (RepositoryTestHooks::managedRecoveryStage) RepositoryTestHooks::managedRecoveryStage(name);
}
void failAt(const QString &name) {
    require(!RepositoryTestHooks::managedFailure || !RepositoryTestHooks::managedFailure(name), "writeFailed", name);
}
void syncPath(const QString &path, bool directory) {
#ifdef Q_OS_UNIX
    int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | (directory ? O_DIRECTORY : 0));
    require(fd >= 0, "writeFailed", path);
    int result;
    do { result = ::fsync(fd); } while (result < 0 && errno == EINTR);
    ::close(fd);
    require(result == 0, "writeFailed", path);
#else
    Q_UNUSED(directory);
    throw ManagedStoreError{"writeFailed", "Durable recovery publication is unavailable on this platform: " + path};
#endif
}
}

struct ManagedLibrary::Lease {
    std::unique_ptr<QFile> instance;
    std::unique_ptr<QLockFile> legacy;
};
std::shared_ptr<ManagedLibrary::Lease> ManagedLibrary::lockDirectory(const QString &directory) {
    checkedDirectory(directory);
    auto lease = std::make_shared<Lease>();
#ifdef Q_OS_LINUX
    // Never remove or replace this inode, including after a crashed sandbox.
    const auto path = directory + "/managed.instance.lock";
    int fd = ::open(QFile::encodeName(path).constData(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    require(fd >= 0, "metadataUnavailable", path);
    lease->instance = std::make_unique<QFile>();
    if (!lease->instance->open(fd, QIODevice::ReadWrite, QFileDevice::AutoCloseHandle)) {
        ::close(fd); throw ManagedStoreError{"metadataUnavailable", path};
    }
    struct stat identity{};
    require(::fstat(fd,&identity)==0 && S_ISREG(identity.st_mode), "unsafePath", path);
    int result;
    do { result = ::flock(fd, LOCK_EX | LOCK_NB); } while (result < 0 && errno == EINTR);
    require(result == 0, "libraryLocked", directory);
    if (!QFileInfo::exists("/.flatpak-info"))
#endif
    {
        const auto path = directory + "/managed.lock";
        require(!QFileInfo(path).isSymLink(), "unsafePath", path);
        lease->legacy = std::make_unique<QLockFile>(path);
        lease->legacy->setStaleLockTime(0);
        require(lease->legacy->tryLock(), "libraryLocked", directory);
    }
    return lease;
}
ManagedLibrary::ManagedLibrary(const QString &directory) {
    require(!directory.isEmpty(), "invalidLibrary");
    entry = QDir(directory).absolutePath();
    // Reject linked ancestors before mkdir can write through one.
    auto ancestor = entry;
    while (!QFileInfo::exists(ancestor) && !QFileInfo(ancestor).isSymLink()) {
        const auto parent = QFileInfo(ancestor).absolutePath();
        require(parent != ancestor, "unsafePath", entry); ancestor = parent;
    }
    checkedDirectory(ancestor);
    require(QDir().mkpath(entry), "metadataUnavailable", entry);
    checkedDirectory(entry);
    entryLease = lockDirectory(entry);
    active = entry;
    const auto pointer = entry + "/active-library.json";
    if (!present(pointer)) return;
    try {
        const QFileInfo info(pointer);
        require(info.isFile() && !info.isSymLink() && info.size() > 0 && info.size() <= 4096, "recoveryPointerInvalid");
        QFile file(pointer);
        require(file.open(QIODevice::ReadOnly), "recoveryPointerInvalid");
        const auto bytes = file.readAll();
        require(file.error() == QFileDevice::NoError, "recoveryPointerInvalid");
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        const auto record = document.object();
        const auto id = record.value("recoveryId").toString();
        const auto name = record.value("directory").toString();
        require(error.error == QJsonParseError::NoError && document.isObject() && record.size() == 3
                && bytes == QJsonDocument(record).toJson(QJsonDocument::Compact)
                && record.value("version").isDouble() && record.value("version").toDouble() == 1
                && validId(id) && name == QFileInfo(entry).fileName() + "-restored-" + id, "recoveryPointerInvalid");
        active = QFileInfo(entry).absolutePath() + '/' + name;
        checkedDirectory(active);
        require(!present(active + "/active-library.json"), "recoveryPointerInvalid");
        require(QFileInfo(active + "/notes.sqlite3").isFile() && !QFileInfo(active + "/notes.sqlite3").isSymLink(), "recoveryPointerInvalid");
    } catch (const ManagedStoreError &) {
        throw ManagedStoreError{"recoveryPointerInvalid", pointer};
    }
    // Do not turn an active-directory lock failure into an invalid-pointer error.
    activeLease = lockDirectory(active);
}
ManagedLibrary::~ManagedLibrary() = default;
QString ManagedLibrary::entryRoot() const { return entry; }
QString ManagedLibrary::activeRoot() const { return active; }
bool ManagedLibrary::activationUncertain() const { return uncertain; }
QString ManagedLibrary::createReplacement(const QString &id) const {
    require(validId(id) && !uncertain, "recoveryPointerInvalid");
    const auto parent = QFileInfo(entry).absolutePath();
    checkedDirectory(parent);
    const auto name = QFileInfo(entry).fileName() + "-restored-" + id;
    failAt("createReplacement");
    require(QDir(parent).mkdir(name), "writeFailed", parent + '/' + name);
    const auto result = parent + '/' + name;
    require(QFile::setPermissions(result, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner), "writeFailed", result);
    return result;
}
void ManagedLibrary::syncFile(const QString &path) { syncPath(path, false); }
void ManagedLibrary::syncDirectory(const QString &path) { syncPath(path, true); }
void ManagedLibrary::publish(const QString &directory, const QString &id, std::shared_ptr<Lease> lease) {
    require(!uncertain && validId(id) && lease, "recoveryPointerInvalid");
    require(directory == QFileInfo(entry).absolutePath() + '/' + QFileInfo(entry).fileName() + "-restored-" + id, "recoveryPointerInvalid");
    checkedDirectory(directory);
    require(!present(directory + "/active-library.json"), "recoveryPointerInvalid");
    const auto pointer = entry + "/active-library.json";
    require(!QFileInfo(pointer).isSymLink(), "recoveryPointerInvalid");
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"recoveryId", id}, {"directory", QFileInfo(directory).fileName()}}).toJson(QJsonDocument::Compact);
    QSaveFile file(pointer); file.setDirectWriteFallback(false);
    failAt("pointerWrite");
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush(), "writeFailed", pointer);
#ifdef Q_OS_UNIX
    int rc;
    do { rc = ::fsync(file.handle()); } while (rc < 0 && errno == EINTR);
    require(rc == 0, "writeFailed", pointer);
#endif
    stage("beforePointerCommit"); failAt("pointerCommit");
    pendingLease = std::move(lease);
    // QSaveFile can report an error while finalizing its atomic rename. Treat
    // any commit error conservatively too; reopening resolves the disk pointer.
    uncertain = true;
    require(file.commit(), "writeFailed", pointer);
    // Any failure past this boundary retains both leases and prohibits writes.
    stage("pointerRenamed"); failAt("pointerDirectorySync");
    syncDirectory(entry);
    active = directory;
    activeLease = std::move(pendingLease);
    uncertain = false;
    stage("pointerDurable");
}

#pragma once
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <QVariantMap>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// The repository supplies a snapshot of validated database identities. Resolving
// an identity never inspects its former location or opens the library database.
namespace ManagedAssets {
inline constexpr qint64 MaxBytes = 32 * 1024 * 1024;
struct Context {
    QString root;
    QUrl documentUrl; // Current file URL, or a library-relative virtual note path.
    QVariantMap manifest; // assets/<sha256>.<extension> -> lowercase SHA-256 of the BLOB.
    bool managed = true;
};

inline QString digestForPath(const QString &path) {
    static const QRegularExpression pattern(QStringLiteral("\\Aassets/([a-f0-9]{64})\\.[a-z0-9]{1,8}\\z"));
    return pattern.match(path).captured(1);
}

namespace detail {
inline bool safeCharacters(const QString &path) {
    for (const QChar c : path) {
        if (c == '\\' || c.category() == QChar::Other_Control || c.category() == QChar::Other_Format)
            return false;
    }
    return !path.isEmpty();
}
inline bool safeUrl(const QUrl &url) {
    if (!url.isValid() || !url.authority().isEmpty()) return false;
    if (!url.scheme().isEmpty() && url.scheme() != "file" && url.scheme() != "nkasset") return false;
    // Decode once: escaped percent/hash characters are ordinary filename bytes.
    // Encoded separators and traversal components never grant another directory.
    static const QRegularExpression encoded(QStringLiteral("%(?:2e|2f|5c|0[0-9a-f]|1[0-9a-f]|7f)"), QRegularExpression::CaseInsensitiveOption);
    if (url.path(QUrl::FullyEncoded).contains(encoded)) return false;
    const QString path = url.path(QUrl::FullyDecoded);
    return safeCharacters(path) && !path.startsWith("//")
        && (url.scheme().isEmpty() || path.startsWith('/'));
}
inline bool plainComponents(const QString &path) {
    if (!safeCharacters(path)) return false;
    for (const auto &part : path.split('/'))
        if (part.isEmpty() || part == "." || part == "..") return false;
    return true;
}
inline QString absolutePath(const QString &path) {
    if (!path.startsWith('/') || !safeCharacters(path)) return {};
    if (path == "/") return path;
    return plainComponents(path.mid(1)) ? path : QString();
}
inline QString rootPath(QString root) {
    while (root.size() > 1 && root.endsWith('/')) root.chop(1);
    return absolutePath(root);
}
inline QString relativeToRoot(const QString &root, const QString &path) {
    const QString prefix = root == "/" ? root : root + '/';
    return !root.isEmpty() && path.startsWith(prefix) ? path.mid(prefix.size()) : QString();
}
inline QString documentPath(const Context &context, const QString &root) {
    if (!safeUrl(context.documentUrl)) return {};
    const QString document = context.documentUrl.path(QUrl::FullyDecoded);
    if (!document.startsWith('/')) return plainComponents(document) ? document : QString();
    if (absolutePath(document).isEmpty()) return {};
    return relativeToRoot(root, document);
}
inline QString scopedRelativePath(const Context &context, const QUrl &url, const QString &root) {
    const QString path = url.path(QUrl::FullyDecoded);
    if (path.startsWith('/')) {
        if (absolutePath(path).isEmpty()) return {};
        return relativeToRoot(root, path);
    }
    const QString document = documentPath(context, root);
    if (document.isEmpty()) return {};
    QStringList parts = document.split('/');
    parts.removeLast();
    bool leading = true;
    for (const auto &part : path.split('/')) {
        if (part.isEmpty()) return {};
        if (part == "." || part == "..") {
            // Ordinary leading ../ is necessary for notes in nested folders.
            // It may never climb out of this library or follow a child and backtrack.
            if (!leading) return {};
            if (part == "..") {
                if (parts.isEmpty()) return {};
                parts.removeLast();
            }
        } else {
            leading = false;
            parts.append(part);
        }
    }
    return parts.join('/');
}
inline QString ownedIdentity(const Context &context, const QString &path) {
    const QString digest = digestForPath(path);
    const auto found = context.manifest.constFind(path);
    if (digest.isEmpty() || found == context.manifest.cend()
        || found->metaType().id() != QMetaType::QString || found->toString() != digest) return {};
    return path;
}
inline QString checkedEntry(const QString &root, const QString &relative) {
    if (!plainComponents(relative)) return {};
    const QString path = (root == "/" ? root : root + '/') + relative;
    QString component;
    const auto parts = path.mid(1).split('/');
    for (qsizetype i = 0; i < parts.size(); ++i) {
        component += '/' + parts[i];
        const QFileInfo info(component);
        if (info.isSymLink() || (i + 1 < parts.size() ? !info.isDir() : !(info.isFile() || info.isDir()))) return {};
    }
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    const QString canonicalFile = QFileInfo(path).canonicalFilePath();
    if (canonicalRoot != root || canonicalFile != path || relativeToRoot(canonicalRoot, canonicalFile).isEmpty()) return {};
    return canonicalFile;
}
inline QString checkedFile(const QString &root, const QString &relative) {
    const QString path = checkedEntry(root, relative);
    if (path.isEmpty()) return {};
    const QFileInfo file(path);
    return file.isFile() && file.size() > 0 && file.size() <= MaxBytes ? path : QString();
}
inline bool readFile(const QString &path, QByteArray &bytes) {
    QFile file;
#ifdef Q_OS_UNIX
    // Walk from / using descriptors so a concurrent symlink substitution cannot
    // make the checked cache path read a different directory (or a FIFO/device).
    const auto parts = path.mid(1).split('/');
#if defined(Q_OS_LINUX) && defined(O_PATH)
    constexpr int directoryFlags = O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
#else
    constexpr int directoryFlags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
#endif
    int directory = ::open("/", directoryFlags);
    if (directory < 0) return false;
    for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
        const int next = ::openat(directory, QFile::encodeName(parts[i]).constData(), directoryFlags);
        ::close(directory);
        if (next < 0) return false;
        directory = next;
    }
    const int descriptor = ::openat(directory, QFile::encodeName(parts.last()).constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    ::close(directory);
    if (descriptor < 0) return false;
    struct stat status {};
    if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size <= 0 || status.st_size > MaxBytes
        || !file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor);
        return false;
    }
#else
    file.setFileName(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
#endif
    bytes = file.read(MaxBytes + 1);
    return file.error() == QFileDevice::NoError && file.atEnd() && !bytes.isEmpty() && bytes.size() <= MaxBytes;
}
} // namespace detail

// Pure identity lookup for rewriting/export from database BLOBs. Absolute URLs
// contribute only their exact assets/<digest>.<ext> suffix (even if the old
// library is now nested under the new root); their old paths are never inspected.
inline QString resolveIdentity(const Context &context, const QUrl &url) {
    if (!context.managed || !detail::safeUrl(url)) return {};
    const QString root = detail::rootPath(context.root);
    if (root.isEmpty()) return {};
    const QString path = url.path(QUrl::FullyDecoded);
    const QString relative = detail::scopedRelativePath(context, url, root);
    const QString owned = detail::ownedIdentity(context, relative);
    if (!owned.isEmpty()) return owned;
    // Historical versions retain their original text after a note moves. A
    // leading relative prefix identifies an owned asset; it never traverses disk.
    if (url.isRelative() && !path.startsWith('/') && !detail::documentPath(context, root).isEmpty()) {
        QString identity = path;
        while (identity.startsWith("../") || identity.startsWith("./"))
            identity.remove(0, identity.startsWith("../") ? 3 : 2);
        const auto historical = detail::ownedIdentity(context, identity);
        if (!historical.isEmpty()) return historical;
    }
    if ((url.scheme() != "file" && url.scheme() != "nkasset") || detail::absolutePath(path).isEmpty()) return {};
    const auto parts = path.split('/');
    if (parts.size() < 3 || parts[parts.size() - 2] != "assets") return {};
    return detail::ownedIdentity(context, "assets/" + parts.last());
}

// Access always verifies current cache bytes against the database-owned digest.
// Pass contents when serving/exporting so the exact verified bytes are consumed,
// rather than reopening a filename that may have changed since validation.
inline QString resolvePath(const Context &context, const QUrl &url, QByteArray *contents = nullptr) {
    if (contents) contents->clear();
    if (!detail::safeUrl(url)) return {};
    const QString root = detail::rootPath(context.root);
    if (root.isEmpty()) return {};
    const QString relative = context.managed ? resolveIdentity(context, url) : detail::scopedRelativePath(context, url, root);
    if (relative.isEmpty()) return {};
    const QString path = detail::checkedFile(root, relative);
    if (path.isEmpty()) return {};
    QByteArray bytes;
    if (!detail::readFile(path, bytes)) return {};
    if (context.managed && QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != context.manifest.value(relative).toString()) return {};
    if (contents) *contents = bytes;
    return path;
}
inline QUrl resolveUrl(const Context &context, const QUrl &url) {
    const QString path = resolvePath(context, url);
    if (path.isEmpty()) return {};
    QUrl result = QUrl::fromLocalFile(path);
    result.setQuery(url.query(QUrl::FullyEncoded), QUrl::StrictMode);
    result.setFragment(url.fragment(QUrl::FullyEncoded), QUrl::StrictMode);
    return result;
}
// Ordinary external links may target directories, empty files or large files.
// Validate their scope without reading them or applying image-serving limits.
inline QUrl resolveExternalLink(const Context &context, const QUrl &url) {
    if (context.managed || !detail::safeUrl(url)) return {};
    const QString root = detail::rootPath(context.root);
    if (root.isEmpty()) return {};
    QUrl target = url;
    QString path = target.path(QUrl::FullyDecoded);
    while (path.size() > 1 && path.endsWith('/')) path.chop(1);
    target.setPath(path);
    const QString relative = detail::scopedRelativePath(context, target, root);
    const QString file = detail::checkedEntry(root, relative);
    if (file.isEmpty()) return {};
    QUrl result = QUrl::fromLocalFile(file);
    result.setQuery(url.query(QUrl::FullyEncoded), QUrl::StrictMode);
    result.setFragment(url.fragment(QUrl::FullyEncoded), QUrl::StrictMode);
    return result;
}
} // namespace ManagedAssets

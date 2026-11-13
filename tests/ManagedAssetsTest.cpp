#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QScopeGuard>
#include "ManagedAssets.h"

class ManagedAssetsTest : public QObject {
    Q_OBJECT
    static bool write(const QString &path, const QByteArray &bytes) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static QString digest(const QByteArray &bytes) {
        return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    }
    static ManagedAssets::Context context(const QString &root, const QString &document, const QByteArray &bytes) {
        const QString hash = digest(bytes);
        return {root, QUrl::fromLocalFile(root + '/' + document), {{"assets/" + hash + ".png", hash}}, true};
    }
    static QUrl schemeUrl(const QString &path) {
        QUrl url;
        url.setScheme("nkasset");
        url.setPath(path);
        return url;
    }
private slots:
    void relativeUrlsAndVirtualDocuments() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray bytes("database image bytes");
        const auto assets = context(directory.path(), "Projects/Nested/note.md", bytes);
        const QString identity = assets.manifest.firstKey();
        const QUrl relative("../../" + identity);
        // Identity resolution also works without a materialized library or note.
        QCOMPARE(ManagedAssets::resolveIdentity(assets, relative), identity);
        QVERIFY(ManagedAssets::resolvePath(assets, relative).isEmpty());
        QVERIFY(write(directory.filePath(identity), bytes));
        QCOMPARE(ManagedAssets::resolveUrl(assets, relative), QUrl::fromLocalFile(directory.filePath(identity)));
        QCOMPARE(ManagedAssets::resolvePath(assets, schemeUrl(directory.filePath(identity))), directory.filePath(identity));
        QCOMPARE(ManagedAssets::resolveIdentity(assets, QUrl::fromLocalFile(directory.filePath(identity))), identity);
        auto virtualDocument = assets;
        virtualDocument.documentUrl = QUrl("Projects/Nested/note.md");
        QCOMPARE(ManagedAssets::resolveIdentity(virtualDocument, relative), identity);
        virtualDocument.documentUrl = QUrl("note.md");
        QCOMPARE(ManagedAssets::resolveIdentity(virtualDocument, QUrl(identity)), identity);
        QCOMPARE(ManagedAssets::resolveIdentity(virtualDocument, QUrl("./" + identity)), identity);
        virtualDocument.root = directory.filePath("not-materialized");
        QCOMPARE(ManagedAssets::resolveIdentity(virtualDocument, QUrl(identity)), identity);
        QVERIFY(ManagedAssets::resolvePath(virtualDocument, QUrl(identity)).isEmpty());
    }
    void oldAbsoluteUrlsUseOnlyTheCurrentCache() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray bytes("authoritative current database bytes");
        const auto assets = context(directory.filePath("Restored Library"), "folder/note.md", bytes);
        const QString identity = assets.manifest.firstKey();
        const QString former = directory.filePath("Old Library/" + identity);
        QVERIFY(write(assets.root + '/' + identity, bytes));
        QVERIFY(write(former, "untrusted bytes at the original location"));
        for (const QUrl &url : {QUrl::fromLocalFile(former), schemeUrl(former)}) {
            QByteArray actual;
            QCOMPARE(ManagedAssets::resolveIdentity(assets, url), identity);
            QCOMPARE(ManagedAssets::resolvePath(assets, url, &actual), assets.root + '/' + identity);
            QCOMPARE(actual, bytes);
        }
        QVERIFY(QFile::remove(former));
        QCOMPARE(ManagedAssets::resolvePath(assets, QUrl::fromLocalFile(former)), assets.root + '/' + identity);
        // Even a symlink at the historical location is irrelevant: it is never opened.
        QVERIFY(QFile::link(assets.root + '/' + identity, former));
        QCOMPARE(ManagedAssets::resolvePath(assets, QUrl::fromLocalFile(former)), assets.root + '/' + identity);
        QVERIFY(QFile::remove(assets.root + '/' + identity));
        QVERIFY(ManagedAssets::resolvePath(assets, QUrl::fromLocalFile(former)).isEmpty());
    }
    void historicalRelativeUrlsKeepTheirOwnedIdentity() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray bytes("historical image bytes");
        auto assets = context(directory.path(), "note.md", bytes);
        const QString identity = assets.manifest.firstKey();
        QVERIFY(write(directory.filePath(identity), bytes));
        for (const QString &prefix : {QString("../"), QString("../../../"), QString("./../../")}) {
            const QUrl url(prefix + identity);
            QCOMPARE(ManagedAssets::resolveIdentity(assets, url), identity);
            QCOMPARE(ManagedAssets::resolvePath(assets, url), directory.filePath(identity));
        }
        assets.documentUrl = QUrl();
        QVERIFY(ManagedAssets::resolveIdentity(assets, QUrl("../" + identity)).isEmpty());
        assets.documentUrl = QUrl("note.md");
        assets.manifest.clear();
        QVERIFY(ManagedAssets::resolveIdentity(assets, QUrl("../" + identity)).isEmpty());
    }
    void escapedFilenamesAndUrlSuffixesArePreserved() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray bytes("image bytes");
        const auto assets = context(directory.filePath("恢复 空间 #%?:"), "文件夹/笔记 #%.md", bytes);
        const QString identity = assets.manifest.firstKey();
        QVERIFY(write(assets.root + '/' + identity, bytes));
        QUrl former = QUrl::fromLocalFile(directory.filePath("原空间 #%?:/" + identity));
        former.setQuery("version=%23%25", QUrl::StrictMode);
        former.setFragment("section%20one", QUrl::StrictMode);
        const QUrl resolved = ManagedAssets::resolveUrl(assets, former);
        QCOMPARE(resolved.toLocalFile(), assets.root + '/' + identity);
        QCOMPARE(resolved.query(QUrl::FullyEncoded), former.query(QUrl::FullyEncoded));
        QCOMPARE(resolved.fragment(QUrl::FullyEncoded), former.fragment(QUrl::FullyEncoded));
        QCOMPARE(ManagedAssets::resolveIdentity(assets, QUrl("../" + identity + "?v=1#part")), identity);
        auto external = assets;
        external.managed = false;
        const QString filename = "图片 #%?:%2f.png";
        QVERIFY(write(assets.root + "/文件夹/" + filename, bytes));
        const QUrl relative(QString::fromUtf8(QUrl::toPercentEncoding(filename)) + "?v=%23#part%25");
        const QUrl result = ManagedAssets::resolveUrl(external, relative);
        QCOMPARE(result.toLocalFile(), assets.root + "/文件夹/" + filename);
        QCOMPARE(result.query(QUrl::FullyEncoded), relative.query(QUrl::FullyEncoded));
        QCOMPARE(result.fragment(QUrl::FullyEncoded), relative.fragment(QUrl::FullyEncoded));
    }
    void databaseOwnershipAndExactContentIdentityAreRequired() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray bytes("owned bytes");
        auto assets = context(directory.path(), "note.md", bytes);
        const QString identity = assets.manifest.firstKey();
        QVERIFY(write(directory.filePath(identity), bytes));
        auto unknown = assets;
        unknown.manifest.clear();
        QVERIFY(ManagedAssets::resolveIdentity(unknown, QUrl(identity)).isEmpty());
        QVERIFY(ManagedAssets::resolvePath(unknown, QUrl::fromLocalFile(directory.filePath(identity))).isEmpty());
        QVERIFY(ManagedAssets::resolveIdentity(assets, QUrl(identity.left(identity.size() - 3) + "jpg")).isEmpty());
        assets.manifest[identity] = digest("another BLOB");
        QVERIFY(ManagedAssets::resolveIdentity(assets, QUrl(identity)).isEmpty());
        assets.manifest = {{"assets/" + digest(bytes).left(32) + ".png", digest(bytes)}};
        QVERIFY(ManagedAssets::resolveIdentity(assets, QUrl(assets.manifest.firstKey())).isEmpty());
        assets.manifest = {{identity, digest(bytes).toUtf8()}};
        QVERIFY(ManagedAssets::resolveIdentity(assets, QUrl(identity)).isEmpty());
        assets.manifest = {{"assets/../" + identity, digest(bytes)}};
        QVERIFY(ManagedAssets::resolveIdentity(assets, QUrl("assets/../" + identity)).isEmpty());
        assets.manifest = {{identity, digest(bytes)}};
        // Old libraries may lie beneath the restored root; only the root cache
        // is exposed even when the original absolute path is itself contained.
        const QUrl nestedOld = QUrl::fromLocalFile(directory.filePath("nested/" + identity));
        QCOMPARE(ManagedAssets::resolveIdentity(assets, nestedOld), identity);
        QCOMPARE(ManagedAssets::resolvePath(assets, nestedOld), directory.filePath(identity));
        QVERIFY(ManagedAssets::resolveIdentity(assets, QUrl("nested/" + identity)).isEmpty());
    }
    void cacheBytesAreVerifiedOnEveryAccess() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray bytes("verified payload");
        const auto assets = context(directory.path(), "note.md", bytes);
        const QString identity = assets.manifest.firstKey();
        const QString file = directory.filePath(identity);
        QVERIFY(write(file, bytes));
        QByteArray actual;
        QCOMPARE(ManagedAssets::resolvePath(assets, QUrl(identity), &actual), file);
        QCOMPARE(actual, bytes);
        QVERIFY(write(file, QByteArray(bytes.size(), 'x')));
        QCOMPARE(ManagedAssets::resolveIdentity(assets, QUrl(identity)), identity);
        QVERIFY(ManagedAssets::resolvePath(assets, QUrl(identity), &actual).isEmpty());
        QVERIFY(actual.isEmpty());
        QVERIFY(write(file, bytes));
        QCOMPARE(ManagedAssets::resolvePath(assets, QUrl(identity)), file);
        QFile oversized(file);
        QVERIFY(oversized.open(QIODevice::WriteOnly));
        QVERIFY(oversized.resize(ManagedAssets::MaxBytes + 1));
        oversized.close();
        QVERIFY(ManagedAssets::resolvePath(assets, QUrl(identity)).isEmpty());
        QVERIFY(write(file, {}));
        QVERIFY(ManagedAssets::resolvePath(assets, QUrl(identity)).isEmpty());
    }
    void malformedAndEscapingUrlsAreRejected() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray bytes("owned bytes");
        const auto assets = context(directory.path(), "folder/note.md", bytes);
        const QString identity = assets.manifest.firstKey();
        QVERIFY(write(directory.filePath(identity), bytes));
        const QString old = "/old/Library/" + identity;
        const QStringList rejected{
            "https://example.org/" + identity,
            "file://server" + old,
            "file://localhost" + old,
            "file://user:password@server" + old,
            "nkasset://server" + old,
            "//server" + old,
            "file:" + identity,
            "nkasset:" + identity,
            "../../Library/" + identity,
            "../nested/../" + identity,
            "../" + identity + "/../other.png",
            "file:///old/../Library/" + identity,
            "nkasset:/old/./Library/" + identity,
            "file:///old//Library/" + identity,
            "..%2f" + identity,
            "%2e%2e/%2e%2e/" + identity,
            "%252e%252e/" + identity,
            "../assets%2f" + identity.mid(7),
            "file:///old%2fLibrary/" + identity,
            "file:///old%5cLibrary/" + identity,
            "file:///old%00/Library/" + identity,
            "file:///old%0a/Library/" + identity,
            "file:///old%7f/Library/" + identity,
            "file:///old\\Library/" + identity,
            "file:////server/Library/" + identity,
            "file:///old/Library/" + identity + "/",
            "/old/Library/" + identity,
            "../assets/" + digest(bytes).toUpper() + ".png"
        };
        for (const QString &input : rejected) {
            const QUrl url(input);
            QVERIFY2(ManagedAssets::resolveIdentity(assets, url).isEmpty(), qPrintable(input));
            QVERIFY2(ManagedAssets::resolvePath(assets, url).isEmpty(), qPrintable(input));
        }
    }
    void symlinkRootsDirectoriesAndFilesAreRejected() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray bytes("owned bytes");
        const auto assets = context(directory.filePath("Library"), "note.md", bytes);
        const QString identity = assets.manifest.firstKey();
        QVERIFY(write(assets.root + '/' + identity, bytes));
        const QString alias = directory.filePath("alias");
        QVERIFY(QFile::link(assets.root, alias));
        auto aliasContext = assets;
        aliasContext.root = alias;
        aliasContext.documentUrl = QUrl::fromLocalFile(alias + "/note.md");
        QVERIFY(ManagedAssets::resolvePath(aliasContext, QUrl(identity)).isEmpty());
        // A symlink above the root is rejected as well.
        QVERIFY(QDir().mkpath(directory.filePath("parent/NestedLibrary")));
        auto nested = context(directory.filePath("parent/NestedLibrary"), "note.md", bytes);
        QVERIFY(write(nested.root + '/' + identity, bytes));
        QVERIFY(QFile::link(directory.filePath("parent"), directory.filePath("parentAlias")));
        nested.root = directory.filePath("parentAlias/NestedLibrary");
        nested.documentUrl = QUrl("note.md");
        QVERIFY(ManagedAssets::resolvePath(nested, QUrl(identity)).isEmpty());
        QVERIFY(QDir().rename(assets.root + "/assets", directory.filePath("outside-assets")));
        QVERIFY(QFile::link(directory.filePath("outside-assets"), assets.root + "/assets"));
        QVERIFY(ManagedAssets::resolvePath(assets, QUrl(identity)).isEmpty());
        QVERIFY(QFile::remove(assets.root + "/assets"));
        QVERIFY(QDir().mkdir(assets.root + "/assets"));
        QVERIFY(QFile::link(directory.filePath("outside-assets/" + identity.mid(7)), assets.root + '/' + identity));
        QVERIFY(ManagedAssets::resolvePath(assets, QUrl(identity)).isEmpty());
    }
    void searchableAncestorsDoNotRequireDirectoryListingPermission() {
#if defined(Q_OS_LINUX) && defined(O_PATH)
        if (::geteuid() == 0) QSKIP("Directory permissions require an unprivileged test user");
        QTemporaryDir fixture;QVERIFY(fixture.isValid());
        const QString ancestor=fixture.filePath("searchable");
        const auto assets=ManagedAssets::Context{ancestor+"/External",QUrl("note.md"),{},false};
        QVERIFY(write(assets.root+"/image.png","readable bytes"));
        const auto restore=qScopeGuard([&]{QFile::setPermissions(ancestor,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);});
        QVERIFY(QFile::setPermissions(ancestor,QFileDevice::ExeOwner));
        QByteArray bytes;
        QCOMPARE(ManagedAssets::resolvePath(assets,QUrl("image.png"),&bytes),assets.root+"/image.png");
        QCOMPARE(bytes,QByteArray("readable bytes"));
#else
        QSKIP("Linux O_PATH traversal regression");
#endif
    }
    void externalNavigationAllowsDirectoriesEmptyFilesAndLargeFiles() {
        QTemporaryDir fixture;QVERIFY(fixture.isValid());
        const auto assets=ManagedAssets::Context{fixture.filePath("External"),QUrl("note.md"),{},false};
        QVERIFY(QDir().mkpath(assets.root+"/documents"));
        QVERIFY(write(assets.root+"/empty.txt",{}));
        QFile video(assets.root+"/recording.mp4");QVERIFY(video.open(QIODevice::WriteOnly));
        QVERIFY(video.resize(ManagedAssets::MaxBytes+1));video.close();
        QCOMPARE(ManagedAssets::resolveExternalLink(assets,QUrl("documents/")).toLocalFile(),assets.root+"/documents");
        QCOMPARE(ManagedAssets::resolveExternalLink(assets,QUrl("empty.txt")).toLocalFile(),assets.root+"/empty.txt");
        const auto link=ManagedAssets::resolveExternalLink(assets,QUrl("recording.mp4?download=1#t=5"));
        QCOMPARE(link.toLocalFile(),video.fileName());QCOMPARE(link.query(),QString("download=1"));QCOMPARE(link.fragment(),QString("t=5"));
        QVERIFY(ManagedAssets::resolvePath(assets,QUrl("recording.mp4")).isEmpty());
        QVERIFY(write(fixture.filePath("outside.txt"),"outside"));
        QVERIFY(ManagedAssets::resolveExternalLink(assets,QUrl("../outside.txt")).isEmpty());
        QVERIFY(QFile::link(fixture.filePath("outside.txt"),assets.root+"/escape.txt"));
        QVERIFY(ManagedAssets::resolveExternalLink(assets,QUrl("escape.txt")).isEmpty());
        auto managed=assets;managed.managed=true;
        QVERIFY(ManagedAssets::resolveExternalLink(managed,QUrl("empty.txt")).isEmpty());
    }
    void externalDocumentsUseOnlyTheirCurrentScope() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray bytes("external image bytes");
        auto assets = context(directory.filePath("External"), "note.md", bytes);
        assets.managed = false;
        const QString identity = assets.manifest.firstKey();
        const QString ownedLookingFile = assets.root + '/' + identity;
        QVERIFY(write(ownedLookingFile, bytes));
        QVERIFY(write(assets.root + "/photo.png", bytes));
        QVERIFY(ManagedAssets::resolveIdentity(assets, QUrl(identity)).isEmpty());
        QCOMPARE(ManagedAssets::resolvePath(assets, QUrl("photo.png")), assets.root + "/photo.png");
        QCOMPARE(ManagedAssets::resolvePath(assets, QUrl(identity)), ownedLookingFile);
        QCOMPARE(ManagedAssets::resolvePath(assets, schemeUrl(ownedLookingFile)), ownedLookingFile);
        const QString outside = directory.filePath("Former/" + identity);
        QVERIFY(write(outside, bytes));
        QVERIFY(ManagedAssets::resolvePath(assets, QUrl::fromLocalFile(outside)).isEmpty());
        QVERIFY(ManagedAssets::resolvePath(assets, schemeUrl(outside)).isEmpty());
        QVERIFY(ManagedAssets::resolvePath(assets, QUrl("../Former/" + identity)).isEmpty());
        QVERIFY(write(assets.root + "-other/photo.png", bytes));
        QVERIFY(ManagedAssets::resolvePath(assets, QUrl::fromLocalFile(assets.root + "-other/photo.png")).isEmpty());
        QVERIFY(QFile::link(outside, assets.root + "/escape.png"));
        QVERIFY(ManagedAssets::resolvePath(assets, QUrl("escape.png")).isEmpty());
        QCOMPARE(ManagedAssets::resolvePath(assets, QUrl("photo.png#fragment")), assets.root + "/photo.png");
        QVERIFY(ManagedAssets::resolvePath(assets, QUrl("file://server" + ownedLookingFile)).isEmpty());
    }
};
QTEST_GUILESS_MAIN(ManagedAssetsTest)
#include "ManagedAssetsTest.moc"

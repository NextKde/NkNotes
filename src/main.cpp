#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QCommandLineParser>
#include <QDir>
#include <QSettings>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QQuickWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include <QFileInfo>
#include <QReadWriteLock>
#include <QStandardPaths>
#include <QSaveFile>
#include <QFile>
#include <QMimeDatabase>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineUrlRequestJob>
#include "EditorSession.h"
#include "EditorRenderLifecycle.h"
#include "NotesController.h"
#include "storage/NotesRepository.h"
class EditorRequests final : public QWebEngineUrlRequestInterceptor {
public:
    void setLibrary(const QString &library) {
        QWriteLocker lock(&m_lock);
        m_library = QFileInfo(library).canonicalFilePath();
    }
    void interceptRequest(QWebEngineUrlRequestInfo &request) override {
        const QUrl url = request.requestUrl();
        bool allowed = url.scheme() == "qrc" && (url.path().startsWith("/editor/") || url.path() == "/qtwebchannel/qwebchannel.js");
        if (url.scheme() == "data" && (request.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeImage || request.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeFontResource)) allowed = true;
        if ((url.isLocalFile() || url.scheme() == "nkasset") && request.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeImage) {
            const QString path = QFileInfo(url.scheme() == "nkasset" ? url.path() : url.toLocalFile()).canonicalFilePath();
            // Only images in the current library are exposed to the document.
            QReadLocker lock(&m_lock);
            const QString root = m_library;
            allowed = !root.isEmpty() && path.startsWith(root + '/');
        }
        request.block(!allowed);
    }
private:
    QReadWriteLock m_lock;
    QString m_library;
};
class EditorAssets final : public QWebEngineUrlSchemeHandler {
public:
    QString library;
    void requestStarted(QWebEngineUrlRequestJob *job) override {
        const QString root = QFileInfo(library).canonicalFilePath();
        const QString path = QFileInfo(job->requestUrl().path()).canonicalFilePath();
        if (job->requestMethod() != "GET" || root.isEmpty() || !path.startsWith(root + '/') || QFileInfo(path).size() > 32 * 1024 * 1024) { job->fail(QWebEngineUrlRequestJob::RequestDenied); return; }
        auto *file = new QFile(path, job);
        if (!file->open(QIODevice::ReadOnly)) { job->fail(QWebEngineUrlRequestJob::UrlNotFound); return; }
        const QByteArray mime = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchContent).name().toUtf8();
        if (!mime.startsWith("image/")) { job->fail(QWebEngineUrlRequestJob::RequestDenied); return; }
        job->reply(mime, file);
    }
};
int main(int argc, char **argv) {
    QWebEngineUrlScheme assetScheme("nkasset");
    assetScheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    assetScheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::CorsEnabled);
    QWebEngineUrlScheme::registerScheme(assetScheme);
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    qmlRegisterType<EditorRenderLifecycle>("NextNotes.Editor", 1, 0, "EditorRenderLifecycle");
    app.setOrganizationName("NextKde");
    app.setApplicationName("NextNotes");
    app.setDesktopFileName("org.nextks.notes");
    app.setWindowIcon(QIcon(QStringLiteral(":/nextnotes/icons/org.nextks.notes.svg")));
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_DICTIONARIES_PATH")) {
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/dictionaries-v1";
        QFile dictionary(":/editor/vendor/dictionaries/en-US.bdic");
        if (dictionary.open(QIODevice::ReadOnly) && QDir().mkpath(folder)) {
            const QString target = folder + "/en-US.bdic";
            if (!QFileInfo::exists(target)) {
                QSaveFile output(target); output.setDirectWriteFallback(false);
                const QByteArray bytes = dictionary.readAll();
                if (output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size()) output.commit();
            }
            qputenv("QTWEBENGINE_DICTIONARIES_PATH", folder.toUtf8());
        }
    }
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument("file", "Open an external Markdown file.", "[file]");
    parser.addOption({"settings-dir", "Store NextNotes preferences in this directory.", "directory"});
    parser.process(app);
    if (parser.isSet("settings-dir")) {
        // Isolate this application's settings without changing XDG_CONFIG_HOME:
        // input-method plugins still need the user's normal theme configuration.
        const QString directory = QDir(parser.value("settings-dir")).absolutePath();
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, directory);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory);
    }
    EditorSession session;
    NotesRepository repository;
    NotesController controller(&session, &repository);
    if (parser.isSet("settings-dir")) controller.setPreference("managedStorage", QDir(parser.value("settings-dir")).absoluteFilePath("Library"));
    bool pendingExternal = !parser.positionalArguments().isEmpty();
    QObject::connect(&controller, &NotesController::changed, &controller, [&] {
        if (pendingExternal && !controller.property("library").toString().isEmpty() && !controller.busy()) {
            pendingExternal = false;
            const QUrl file = QUrl::fromLocalFile(QFileInfo(parser.positionalArguments().first()).absoluteFilePath());
            QMetaObject::invokeMethod(&controller, [&controller, file] { controller.openExternal(file); }, Qt::QueuedConnection);
        }
    });
    EditorRequests requests;
    EditorAssets assets;
    QQuickWebEngineProfile::defaultProfile()->installUrlSchemeHandler("nkasset", &assets);
    QObject::connect(&controller, &NotesController::changed, &assets, [&] { assets.library = controller.assetRoot(); });
    QObject::connect(&controller, &NotesController::changed, &requests, [&] { requests.setLibrary(controller.assetRoot()); });
    QQuickWebEngineProfile::defaultProfile()->setUrlRequestInterceptor(&requests);
    QQuickWebEngineProfile::defaultProfile()->setSpellCheckEnabled(true);
    QQuickWebEngineProfile::defaultProfile()->setSpellCheckLanguages({"en-US"});
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"session", QVariant::fromValue(&session)}, {"controller", QVariant::fromValue(&controller)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("NextNotes", "Main");
    const int result = app.exec();
    QQuickWebEngineProfile::defaultProfile()->setUrlRequestInterceptor(nullptr);
    QQuickWebEngineProfile::defaultProfile()->removeUrlSchemeHandler(&assets);
    return result;
}

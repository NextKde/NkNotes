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
#include <QBuffer>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineUrlRequestJob>
#include "EditorSession.h"
#include "EditorRenderLifecycle.h"
#include "NotesController.h"
#include "storage/NotesRepository.h"
#include "storage/ManagedAssets.h"

// Interception runs off the UI thread. Publish one coherent, implicitly shared
// repository snapshot for both interception and serving; never query SQL here.
class AssetContext {
public:
    void publish(const ManagedAssets::Context &context) {
        QWriteLocker lock(&m_lock);
        m_context = context;
    }
    ManagedAssets::Context snapshot() const {
        QReadLocker lock(&m_lock);
        return m_context;
    }
private:
    mutable QReadWriteLock m_lock;
    ManagedAssets::Context m_context;
};
class EditorRequests final : public QWebEngineUrlRequestInterceptor {
public:
    explicit EditorRequests(const AssetContext &context) : m_context(context) {}
    void interceptRequest(QWebEngineUrlRequestInfo &request) override {
        const QUrl url = request.requestUrl();
        bool allowed = url.scheme() == "qrc" && (url.path().startsWith("/editor/") || url.path() == "/qtwebchannel/qwebchannel.js");
        if (url.scheme() == "data" && (request.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeImage || request.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeFontResource)) allowed = true;
        if ((url.isLocalFile() || url.scheme() == "nkasset") && request.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeImage) {
            const auto context = m_context.snapshot();
            const QString identity = context.managed ? ManagedAssets::resolveIdentity(context, url) : QString();
            const QString path = context.managed
                ? (identity.isEmpty() ? QString() : QDir(context.root).filePath(identity))
                : ManagedAssets::resolvePath(context, url);
            allowed = !path.isEmpty();
            if (allowed && url.isLocalFile()) {
                // Chromium must never open a historical absolute file URL. The
                // scheme handler checks current cache bytes against the manifest.
                QUrl controlled;
                controlled.setScheme("nkasset");
                controlled.setPath(path);
                controlled.setQuery(url.query(QUrl::FullyEncoded), QUrl::StrictMode);
                controlled.setFragment(url.fragment(QUrl::FullyEncoded), QUrl::StrictMode);
                request.redirect(controlled);
                return;
            }
        }
        request.block(!allowed);
    }
private:
    const AssetContext &m_context;
};
class EditorAssets final : public QWebEngineUrlSchemeHandler {
public:
    explicit EditorAssets(const AssetContext &context) : m_context(context) {}
    void requestStarted(QWebEngineUrlRequestJob *job) override {
        QByteArray bytes;
        if (job->requestMethod() != "GET" || ManagedAssets::resolvePath(m_context.snapshot(), job->requestUrl(), &bytes).isEmpty()) {
            job->fail(QWebEngineUrlRequestJob::RequestDenied);
            return;
        }
        const QByteArray mime = QMimeDatabase().mimeTypeForData(bytes).name().toUtf8();
        if (!mime.startsWith("image/")) { job->fail(QWebEngineUrlRequestJob::RequestDenied); return; }
        auto *buffer = new QBuffer(job);
        buffer->setData(bytes);
        buffer->open(QIODevice::ReadOnly);
        job->reply(mime, buffer);
    }
private:
    const AssetContext &m_context;
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
    AssetContext assetContext;
    EditorRequests requests(assetContext);
    EditorAssets assets(assetContext);
    QQuickWebEngineProfile::defaultProfile()->installUrlSchemeHandler("nkasset", &assets);
    const auto publishAssets = [&] {
        assetContext.publish({controller.assetRoot(), controller.documentUrl(), controller.assetManifest(), controller.managedAssets()});
    };
    // The controller emits changed after accepting the manifest and before
    // loading document content or delivering an attachment result to WebChannel.
    QObject::connect(&controller, &NotesController::changed, &assets, publishAssets);
    publishAssets();
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

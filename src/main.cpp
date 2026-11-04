#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "EditorSession.h"
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("NextKde");
    app.setApplicationName("NextNotes");
    EditorSession session;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"session", QVariant::fromValue(&session)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("NextNotes", "Main");
    return app.exec();
}

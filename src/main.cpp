#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "EditorSession.h"
#include "NotesController.h"
#include "storage/NotesRepository.h"
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("NextKde");
    app.setApplicationName("NextNotes");
    EditorSession session;
    NotesRepository repository;
    NotesController controller(&session, &repository);
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"session", QVariant::fromValue(&session)}, {"controller", QVariant::fromValue(&controller)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("NextNotes", "Main");
    return app.exec();
}

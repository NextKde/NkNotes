#include <QtQuickTest/quicktest.h>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QtQml/qqml.h>
#include "EditorRenderLifecycle.h"

int main(int argc, char **argv)
{
    QtWebEngineQuick::initialize();
    qmlRegisterType<EditorRenderLifecycle>("NextNotes.Editor", 1, 0, "EditorRenderLifecycle");
    return quick_test_main(argc, argv, "editor-render", QUICK_TEST_SOURCE_DIR);
}

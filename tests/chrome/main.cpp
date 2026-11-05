#include <QtQuickTest/quicktest.h>
#include <QQuickStyle>

int main(int argc, char **argv)
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    return quick_test_main(argc, argv, "notes-chrome", QUICK_TEST_SOURCE_DIR);
}

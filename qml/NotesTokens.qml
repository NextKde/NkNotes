import QtQuick
import Kos.Ui
QtObject {
    readonly property color sidebarBackground: AppTheme.dark ? AppTheme.contentSecondaryBackground : "#eeedea"
    readonly property color primaryYellow: "#ffb921"
    readonly property color navigationSelected: AppTheme.dark ? "#454544" : "#d9d8d3"
    readonly property color navigationSelectedText: AppTheme.dark ? "#ffffff" : "#000000"
    readonly property color notesYellow: AppTheme.dark ? "#bc8b16" : primaryYellow
    readonly property string uiFontFamily: "Noto Sans CJK SC"
    readonly property int windowWidth: 1360
    readonly property int windowHeight: 800
    readonly property int sidebarWidth: 224
    readonly property int listWidth: 285
    readonly property int toolbarHeight: 47
    readonly property int navigationSize: 13
    readonly property int navigationHeight: 28
    readonly property int noteRowHeight: 68
    readonly property int bodySize: 16
    readonly property int readingWidth: 1200
    readonly property int inset: 26
    readonly property int editorInset: 48
    readonly property int documentTitleSize: 24
}

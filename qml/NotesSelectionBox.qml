import QtQuick
import QtQuick.Controls
import Kos.Ui

// Match the calendar sidebar's compact category checkbox.
CheckBox {
    id: control
    property color accentColor: AppTheme.accent
    implicitWidth: 24
    implicitHeight: 24
    padding: 5
    hoverEnabled: true
    background: null
    contentItem: Item {}
    indicator: Rectangle {
        implicitWidth: 14
        implicitHeight: 14
        x: (control.width - width) / 2
        y: (control.height - height) / 2
        radius: 3
        color: control.checked ? control.accentColor : "transparent"
        border.width: control.checked ? 0 : 1
        border.color: control.accentColor
        opacity: control.enabled ? 1 : 0.48
        VectorIcon {
            anchors.centerIn: parent
            size: 12
            name: "check"
            color: "white"
            visible: control.checked
        }
    }
}

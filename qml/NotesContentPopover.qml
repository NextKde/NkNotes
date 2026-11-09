import QtQuick
import QtQuick.Controls
import Kos.Ui
import "PopupPlacement.js" as Placement

LiquidContentPopover {
    id: root
    property real preferredWidth: 350
    property string invocation: "button"
    property point contextPosition: Qt.point(0, 0)
    preferredSide: "bottom"
    margins: 8
    width: Math.min(preferredWidth, parent ? parent.width - margins * 2 : preferredWidth)
    arrowEnabled: invocation === "button"
    // Reserve the pointer space even when an edge hides it, keeping sizing stable.
    leftPadding: padding; rightPadding: padding
    topPadding: padding + (arrowEnabled && resolvedSide !== "top" ? 8 : 0)
    bottomPadding: padding + (arrowEnabled && resolvedSide === "top" ? 8 : 0)

    function updatePosition() {
        if (!parent || (invocation !== "context" && !anchorItem)) { arrowFits = false; return }
        const p = invocation === "context" ? Placement.atPoint(root, contextPosition)
                                          : Placement.atAnchor(root, anchorItem, false)
        resolvedSide = p.above ? "top" : "bottom"
        x = p.x; y = p.y; arrowPosition = p.arrow; arrowFits = p.arrowFits
    }
    function showPositioned() { updatePosition(); open(); Qt.callLater(updatePosition) }
    function openFromButton(item) { invocation = "button"; anchorItem = item; showPositioned() }
    function openFromMenu(menu) {
        const from = menu.origin()
        menu.close()
        Qt.callLater(function() {
            root.invocation = from.invocation === "context" ? "context" : "button"
            root.contextPosition = from.point
            root.anchorItem = from.item
            root.showPositioned()
        })
    }
    onAboutToShow: updatePosition()
    onOpened: Qt.callLater(updatePosition)
    onWidthChanged: if (visible) updatePosition()
    onHeightChanged: if (visible) updatePosition()
    Connections {
        target: root.parent
        function onWidthChanged() { if (root.visible) root.updatePosition() }
        function onHeightChanged() { if (root.visible) root.updatePosition() }
    }
}

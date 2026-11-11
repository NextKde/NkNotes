import QtQuick
import QtQuick.Controls
import Kos.Ui
import "PopupPlacement.js" as Placement

// Application placement policy; rendering and menu keyboard behaviour stay in Kos.Ui.
LiquidPopover {
    id: root
    property string invocation: "button"
    property point contextPosition: Qt.point(0, 0)
    preferredSide: "bottom"
    anchorAlignment: "center"
    arrowEnabled: invocation === "button"
    // Keep height limits in one binding for both caret and button menus.
    property real maxHeight: Number.POSITIVE_INFINITY
    height: Math.min(implicitHeight, maxHeight, parent ? parent.height - margins * 2 : implicitHeight)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutside

    function updatePosition() {
        if (!parent || (invocation !== "context" && !anchorItem)) { arrowFits = false; return }
        const p = invocation === "context" ? Placement.atPoint(root, contextPosition)
                                          : Placement.atAnchor(root, anchorItem, invocation === "caret")
        x = p.x; y = p.y; arrowOnBottom = p.above; arrowX = p.arrow; arrowFits = p.arrowFits
    }
    function showPositioned() { updatePosition(); open(); Qt.callLater(updatePosition) }
    function openFromButton(item) { invocation = "button"; anchorItem = item; showPositioned() }
    function openForCaret(item) { invocation = "caret"; anchorItem = item; showPositioned() }
    function openAtPoint(item, position) {
        const p = position || Qt.point(item.width / 2, item.height / 2)
        contextPosition = item.mapToItem(parent, p.x, p.y)
        invocation = "context"; anchorItem = null; showPositioned()
    }
    function origin() { return {invocation: invocation, item: anchorItem, point: contextPosition} }
    function openFromMenu(menu) {
        const from = menu.origin()
        menu.close()
        Qt.callLater(function() {
            if (from.invocation === "context") root.openAtPoint(root.parent, from.point)
            else if (from.item) root.openFromButton(from.item)
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

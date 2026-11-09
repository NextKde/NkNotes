.pragma library

function clamp(value, minimum, maximum) {
    return Math.max(minimum, Math.min(value, maximum))
}

function atPoint(popup, point) {
    const edge = popup.margins
    return {x: clamp(point.x, edge, popup.parent.width - popup.width - edge),
            y: clamp(point.y, edge, popup.parent.height - popup.height - edge),
            above: false, arrow: 0, arrowFits: false}
}

// Button menus and forms share one vertical placement rule. Only window edges
// can shift them off centre or move them above the trigger.
function atAnchor(popup, anchor, caret) {
    const edge = popup.margins, gap = 4
    const point = anchor.mapToItem(popup.parent, 0, 0)
    const centre = point.x + anchor.width / 2
    const below = point.y + anchor.height + gap
    const aboveY = point.y - gap - popup.height
    const belowFits = below + popup.height <= popup.parent.height - edge
    const aboveFits = aboveY >= edge
    const above = !belowFits && (aboveFits || point.y > popup.parent.height - point.y - anchor.height)
    const x = clamp(caret ? point.x : centre - popup.width / 2, edge, popup.parent.width - popup.width - edge)
    const y = clamp(above ? aboveY : below, edge, popup.parent.height - popup.height - edge)
    const arrow = centre - x, inset = popup.cornerRadius + popup.arrowWidth / 2
    return {x: x, y: y, above: above, arrow: arrow,
            arrowFits: !caret && (above ? y + popup.height <= point.y - gap : y >= below)
                && arrow >= inset && arrow <= popup.width - inset
                && point.y >= 0 && point.y + anchor.height <= popup.parent.height}
}

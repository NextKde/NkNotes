#include "EditorRenderLifecycle.h"
#include <QTimer>
#include <utility>

void EditorRenderLifecycle::setView(QQuickItem *view)
{
    if (m_view == view) return;
    for (auto *item : std::as_const(m_watched)) QObject::disconnect(item, nullptr, this, nullptr);
    m_watched.clear();
    m_retained.clear();
    m_view = view;
    if (m_attached) { m_attached = false; emit attachedChanged(); }
    emit viewChanged();
    scheduleScan();
}

void EditorRenderLifecycle::scheduleScan()
{
    if (m_scheduled) return;
    m_scheduled = true;
    // Wait for delegate construction/window attachment to finish connecting.
    QTimer::singleShot(0, this, [this] { m_scheduled = false; scan(); });
}

void EditorRenderLifecycle::scan()
{
    bool attached = false;
    QList<QQuickItem *> pending;
    if (m_view) pending.append(m_view);
    while (!pending.isEmpty()) {
        auto *item = pending.takeLast();
        pending.append(item->childItems());
        if (!m_watched.contains(item)) {
            m_watched.insert(item);
            connect(item, &QQuickItem::childrenChanged, this, &EditorRenderLifecycle::scheduleScan);
            connect(item, &QQuickItem::windowChanged, this, [this, item] {
                m_retained.remove(item);
                if (m_attached) { m_attached = false; emit attachedChanged(); }
                scheduleScan();
            });
            connect(item, &QObject::destroyed, this, [this, item] {
                m_watched.remove(item);
                m_retained.remove(item);
                scheduleScan();
            });
        }
        const QByteArray name(item->metaObject()->className());
        if (name != "QtWebEngineCore::RenderWidgetHostViewQtDelegateItem"
            && name != "RenderWidgetHostViewQtDelegateItem") continue;
        auto *window = item->window();
        if (!window) continue;
        if (m_retained.value(item) != window) {
            // A paused render loop can retain its scene graph. Releasing the
            // imported WebEngine surface here can leave that graph drawing black
            // until Chromium produces a new frame. Keep the surface on pause;
            // actual sceneGraphInvalidated and delegate destruction still clean up.
            // Do not reload the page: it owns unsaved edits, IME and undo history.
            if (QObject::disconnect(window, &QQuickWindow::sceneGraphAboutToStop, item, nullptr))
                m_retained.insert(item, window);
        }
        attached |= m_retained.value(item) == window;
    }
    if (m_attached != attached) {
        m_attached = attached;
        emit attachedChanged();
    }
}

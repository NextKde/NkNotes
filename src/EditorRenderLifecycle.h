#pragma once

#include <QHash>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>

// WebEngine compatibility adapter, shared in approach with NkMail's reader.
// Public Qt APIs are used, but the delegate class identity is private knowledge:
// review the real-WebEngine regression when upgrading Qt.
class EditorRenderLifecycle : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickItem *view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(bool attached READ attached NOTIFY attachedChanged)
public:
    explicit EditorRenderLifecycle(QObject *parent = nullptr) : QObject(parent) {}
    QQuickItem *view() const { return m_view; }
    void setView(QQuickItem *view);
    bool attached() const { return m_attached; }
signals:
    void viewChanged();
    void attachedChanged();
private:
    void scheduleScan();
    void scan();
    QPointer<QQuickItem> m_view;
    QSet<QQuickItem *> m_watched;
    QHash<QQuickItem *, QPointer<QQuickWindow>> m_retained;
    bool m_scheduled = false;
    bool m_attached = false;
};

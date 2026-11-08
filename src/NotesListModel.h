#pragma once
#include <QAbstractListModel>
#include <QVariantMap>

// Keep delegates and their hover/focus state alive across autosave listings.
class NotesListModel final : public QAbstractListModel {
public:
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override {
        return role == Qt::UserRole && index.isValid() && index.row() >= 0 && index.row() < m_rows.size() ? m_rows[index.row()] : QVariant();
    }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "modelData"}}; }
    void replace(const QVariantList &rows) {
        if (rows == m_rows) return;
        // A large sort must not search and move the remaining rows one by one.
        // Preserve persistent indexes by identity while publishing one layout change.
        if (rows.size() > 512 && rows.size() == m_rows.size()) {
            QHash<QString,int> destinations;
            for (int i=0;i<rows.size();++i) destinations.insert(key(rows[i]),i);
            bool sameIdentities=destinations.size()==rows.size(), reordered=false;
            for (int i=0;sameIdentities && i<m_rows.size();++i) {
                const auto found=destinations.constFind(key(m_rows[i]));
                if(found==destinations.cend())sameIdentities=false;
                else if(found.value()!=i)reordered=true;
            }
            if (sameIdentities) {
                if (reordered) {
                    emit layoutAboutToBeChanged({},QAbstractItemModel::VerticalSortHint);
                    const auto previous=persistentIndexList();QModelIndexList next;
                    for(const auto &p:previous)next.append(index(destinations.value(key(m_rows[p.row()]))));
                    m_rows=rows;changePersistentIndexList(previous,next);
                    emit layoutChanged({},QAbstractItemModel::VerticalSortHint);
                } else m_rows=rows;
                emit dataChanged(index(0),index(m_rows.size()-1),{Qt::UserRole});
                return;
            }
        }
        // Bulk imports and scope changes publish one coherent replacement. Thousands
        // of individual inserts leave QML delegates in a long intermediate layout.
        if (qMax(rows.size(), m_rows.size()) > 512 || qAbs(rows.size() - m_rows.size()) > 64) {
            beginResetModel(); m_rows = rows; endResetModel(); return;
        }
        for (int i = 0; i < rows.size(); ++i) {
            int existing = i;
            while (existing < m_rows.size() && key(m_rows[existing]) != key(rows[i])) ++existing;
            if (existing == m_rows.size()) {
                beginInsertRows({}, i, i); m_rows.insert(i, rows[i]); endInsertRows();
            } else {
                if (existing != i) {
                    beginMoveRows({}, existing, existing, {}, i);
                    m_rows.move(existing, i); endMoveRows();
                }
                if (m_rows[i] != rows[i]) { m_rows[i] = rows[i]; emit dataChanged(index(i), index(i), {Qt::UserRole}); }
            }
        }
        if (m_rows.size() > rows.size()) {
            beginRemoveRows({}, rows.size(), m_rows.size() - 1);
            m_rows.erase(m_rows.begin() + rows.size(), m_rows.end()); endRemoveRows();
        }
    }
private:
    static QString key(const QVariant &row) { const auto value = row.toMap(); return value.value("id").toString() + '\n' + value.value("path").toString(); }
    QVariantList m_rows;
};

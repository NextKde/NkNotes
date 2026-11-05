#include "NotesController.h"
#include "EditorSession.h"
#include <QCryptographicHash>
#include <QSettings>

QString NotesController::sortPreferenceKey() const {
    return "sorting/" + QString::fromLatin1(QCryptographicHash::hash((m_library + '\n' + m_filter).toUtf8(), QCryptographicHash::Sha256).toHex());
}
QString NotesController::sortKey() const { return preference(sortPreferenceKey() + "/key", "modified").toString(); }
bool NotesController::sortAscending() const { return preference(sortPreferenceKey() + "/ascending", false).toBool(); }
void NotesController::setSortKey(const QString &key) {
    if (!QStringList{"modified", "created", "title"}.contains(key) || key == sortKey()) return;
    setPreference(sortPreferenceKey() + "/key", key);
    setPreference(sortPreferenceKey() + "/ascending", key == "title");
    ++m_searchSerial; refresh(); emit changed();
}
void NotesController::setSortAscending(bool value) {
    if (value == sortAscending()) return;
    setPreference(sortPreferenceKey() + "/ascending", value);
    ++m_searchSerial; refresh(); emit changed();
}
QString NotesController::organizerError(const QString &code) const {
    const QHash<QString, QString> messages{
        {"invalidProperty", "标签不能为空、超过 100 字或包含换行"}, {"historyWriteFailed", "历史写入失败，原文件保留"}, {"conflict", "文件已变化，请刷新后重试"}, {"unsafePath", "路径无效、目录不存在或包含符号链接"},
        {"alreadyExists", "目标已有同名文件，未覆盖"}, {"pendingDrafts", "存在待处理的恢复草稿，请先处理"},
        {"pendingRelocation", "有未完成的移动操作，请在更多菜单恢复移动操作"},
        {"unsupportedLinks", "引用格式暂不支持或目标存在歧义，未移动"},
        {"trashChanged", "回收内容已变化，请刷新后重新选择"},
        {"purgeIncomplete", "删除未完成，记录保留；请刷新后重试永久删除"},
        {"relativeLinks", "包含链接引用，暂不能安全跨文件夹移动；原文件保留"},
        {"folderNotEmpty", "此操作仅适用于空文件夹，请先移动其中内容"},
        {"invalidBatch", "请选择 1 至 200 篇笔记及有效操作"}, {"notListed", "笔记不在当前结果中，请刷新后重选"},
        {"duplicateRequest", "重复选择了同一文件"}, {"unsupportedFormat", "文件格式无法安全生成副本"},
        {"metadataWriteFailed", "元数据写入失败，请检查笔记库权限和空间"}, {"writeFailed", "写入失败，原文件保留"},
        {"readFailed", "无法读取文件，请检查文件和权限"}, {"moveFailed", "移动失败，请检查目录权限"}
    };
    return messages.value(code, "操作未完成：" + code);
}
void NotesController::organize(const QString &action, const QStringList &paths, const QString &folder) {
    if (paths.isEmpty() || paths.size() > 200 || busy()) return;
    drain([this, action, paths, folder] {
        QVariantList items;
        for (const auto &path : paths) {
            QString hash;
            for (const auto &value : m_notes) {
                const auto note = value.toMap();
                if (note.value("path").toString() == path) { hash = note.value("baseHash").toString(); break; }
            }
            // drain may have just saved while the search listing is still in flight.
            if (path == m_path) hash = m_base;
            items.append(QVariantMap{{"path", path}, {"baseHash", hash}});
        }
        m_organizing = true; m_status = QString("正在处理 %1 篇笔记…").arg(paths.size());
        send("batch", {{"action", action}, {"items", items}, {"folder", folder}}); emit changed();
    });
}
void NotesController::manageFolder(const QString &operation, const QString &path, const QString &target) {
    if (busy() || !QStringList{"renameFolder", "removeFolder", "trashFolder"}.contains(operation)) return;
    drain([this, operation, path, target] {
        m_organizing = true; m_status = "正在整理文件夹…";
        send(operation, {{"path", path}, {"target", target}}); emit changed();
    });
}
void NotesController::acceptBatch(QVariantMap report) {
    QVariantList results; QString moved;
    for (const auto &value : report.value("results").toList()) {
        auto result = value.toMap();
        if (result.value("error") == "pendingRelocation") m_readOnly = true;
        if (!result.value("ok").toBool()) result.insert("message", organizerError(result.value("error").toString()));
        else if (report.value("action") == "move") {
            relocateHistory(result.value("path").toString(), result.value("newPath").toString());
            if (result.value("path").toString() == m_path) moved = result.value("newPath").toString();
        }
        else if (result.value("path").toString() == m_path) {
            if (report.value("action") == "trash") {
                ++m_epoch; m_path.clear(); m_selectedId.clear(); m_base.clear(); m_revision = m_savedRevision = 0;
                m_applying = true; m_session->loadPrototype({}); m_applying = false;
                m_readOnly = false; m_conflict = false;
            } else if (report.value("action") == "move" && result.value("newPath").toString() != m_path) moved = result.value("newPath").toString();
        }
        results.append(result);
    }
    report.insert("results", results); m_batchReport = report;
    m_status = QString("完成 %1 项，失败 %2 项").arg(report.value("succeeded").toInt()).arg(report.value("failed").toInt());
    if (!moved.isEmpty()) { m_filter = "all"; ++m_searchSerial; load(moved); }
    if (moved.isEmpty() && (report.value("action") == "move" || report.value("action") == "tags") && !m_path.isEmpty()) load(m_path);
    refresh(); inspectRecovery(); emit changed(); emit batchFinished();
}

void NotesController::recycle(const QString &action,const QVariantList &items) {
    if(busy()||items.isEmpty()||!QStringList{"restore","purge"}.contains(action))return;
    drain([this,action,items]{m_organizing=true;send("trashBatch",{{"action",action},{"items",items}});emit changed();});
}

#include "NotesController.h"
#include "EditorSession.h"
#include "storage/MarkdownFile.h"
#include "storage/ManagedAssets.h"
#include "storage/NotesRepository.h"
#include <QDir>
#include <QFileInfo>

QString NotesController::assetRoot() const {
    return externalDocument() ? QFileInfo(m_path.mid(9)).absolutePath() : m_library;
}
QUrl NotesController::documentUrl() const {
    return QUrl::fromLocalFile(externalDocument() ? m_path.mid(9) : m_library + '/' + m_path);
}
QUrl NotesController::resolveAsset(const QUrl &source) const {
    return ManagedAssets::resolveUrl({assetRoot(), documentUrl(), assetManifest(), managedAssets()}, source);
}
bool NotesController::systemFileDialogs() const { return QFileInfo::exists("/.flatpak-info"); }
QString NotesController::legacyLibrary() const { return preference("legacyLibrary").toString(); }
void NotesController::previewImport(const QList<QUrl> &sources,const QString &folder,bool keepStructure) {
    if (busy() || sources.isEmpty()) return;
    QStringList paths;for(const auto &source:sources)if(source.isLocalFile())paths.append(source.toString());
    if(paths.isEmpty())return;
    drain([this,paths,folder,keepStructure]{m_operationProgress={{"phase","scanning"},{"completed",0}};m_organizing=true;m_importPlan.clear();m_status="正在读取导入内容…";send("importPreview",{{"sources",paths},{"folder",folder},{"keepStructure",keepStructure}});emit changed();});
}
void NotesController::confirmImport(bool keepDuplicates) {
    if (busy() || m_importPlan.isEmpty()) return;
    const auto plan=m_importPlan;
    drain([this,plan,keepDuplicates]{m_operationProgress={{"phase","preparing"},{"completed",0}};m_organizing=true;m_status="正在导入…";send("importNotes",{{"plan",plan},{"duplicates",keepDuplicates?"copy":"skip"}});emit changed();});
}
void NotesController::inspectImports() { if(!m_library.isEmpty())send("imports"); }
void NotesController::openExternal(const QUrl &source) {
    if (busy() || !source.isLocalFile()) return;
    const QFileInfo file(source.toLocalFile());
    if(!file.isFile()||file.suffix().compare("md",Qt::CaseInsensitive)){m_error="请选择 Markdown 文件";emit changed();return;}
    const auto path="external:"+file.canonicalFilePath();drain([this,path]{load(path);});
}
void NotesController::importExternal() {
    if(!externalDocument()||busy())return;
    const auto source=QUrl::fromLocalFile(m_path.mid(9));previewImport({source});
}
void NotesController::exportNotes(const QUrl &destination,const QStringList &paths,const QString &folder) {
    if(busy()||!destination.isLocalFile())return;
    drain([this,destination,paths,folder]{m_organizing=true;m_status="正在导出…";send("exportNotes",{{"destination",destination.toString()},{"paths",paths},{"folder",folder}});emit changed();});
}

void NotesController::renameNote(const QString &title) {
    if (busy() || externalDocument() || m_path.isEmpty() || title.trimmed().isEmpty()) return;
    drain([this,title]{m_organizing=true;send("renameNote",{{"path",m_path},{"baseHash",m_base},{"title",title.trimmed()}});emit changed();});
}

void NotesController::cancelImport() {
    if (busy()) return;
    m_importPlan.clear(); m_status = m_path.isEmpty() ? "请选择笔记" : dirty() ? "已修改" : "已保存"; emit changed();
}

void NotesController::cancelImportOperation() {
    if(m_repository && !m_operationProgress.isEmpty()) {
        m_repository->cancelOperation();m_operationProgress.insert("cancelling",true);emit changed();
    }
}
void NotesController::createBackup(const QUrl &destination) {
    if(busy() || !destination.isLocalFile())return;
    drain([this,destination]{m_organizing=true;m_status="正在创建完整备份…";send("backup",{{"destination",destination.toString()}});emit changed();});
}
void NotesController::restoreBackup(const QUrl &source) {
    if(busy() || !source.isLocalFile())return;
    drain([this,source]{m_organizing=true;m_status="正在检查并恢复备份…";send("restoreBackup",{{"source",source.toString()}});emit changed();});
}

QString NotesController::noteTitle() const { return MarkdownFile::title(m_session->source(), "未命名笔记"); }

void NotesController::closeExternal() {
    if (!externalDocument() || busy()) return;
    drain([this] {
        ++m_epoch;m_path.clear();m_selectedId.clear();m_base.clear();m_revision=m_savedRevision=0;
        m_history.clear();m_historyIndex=-1;m_historyTarget=-1;m_conflict=false;m_readOnly=false;
        m_knowledge.clear();m_versions.clear();m_version.clear();m_missingLink.clear();
        m_applying=true;m_session->loadPrototype({});m_applying=false;
        m_query.clear();m_filter="all";++m_searchSerial;
        setPreference("lastNote",QVariantMap{});m_status="请选择笔记";refresh();emit changed();
    });
}

void NotesController::selectCategory(const QString &filter, const QString &query) {
    if (busy()) return;
    drain([this,filter,query] {
        const auto parent=QFileInfo(m_path).path();
        const bool inCategory=query.isEmpty() && !externalDocument() && (filter=="all" || (filter=="favorite" && m_favorite) ||
            (filter=="inbox" && parent==".") || (filter.startsWith("folder:") && parent==filter.mid(7)));
        m_query=query;m_filter=filter;++m_searchSerial;
        if (!m_path.isEmpty() && !inCategory) {
            ++m_epoch;m_path.clear();m_selectedId.clear();m_base.clear();m_revision=m_savedRevision=0;
            m_conflict=false;m_readOnly=false;m_knowledge.clear();m_versions.clear();m_version.clear();m_missingLink.clear();
            m_applying=true;m_session->loadPrototype({});m_applying=false;
            setPreference("lastNote",QVariantMap{});m_status="请选择笔记";
        }
        refresh();emit changed();
    });
}

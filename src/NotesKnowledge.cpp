#include "NotesController.h"
#include "EditorSession.h"
#include <QUrl>

void NotesController::inspectKnowledge(){if(!m_path.isEmpty())send("knowledge",{{"path",m_path}});}
void NotesController::lookupLinks(const QString &query,int request){if(!m_library.isEmpty())send("linkCandidates",{{"query",query},{"request",request}});}
void NotesController::setNoteProperty(const QString &key,const QVariant &value,bool remove){
    if(m_path.isEmpty()||busy()||readOnly())return;
    // Detach QML arrays on the GUI thread before queuing work or waiting for a save.
    const QVariant stored=key=="tags"?QVariant(value.toStringList()):QVariant(value.toString());
    drain([this,key,stored,remove]{m_organizing=true;send("setProperty",{{"path",m_path},{"baseHash",m_base},{"key",key},{"value",stored},{"remove",remove}});emit changed();});
}
void NotesController::inspectHistory(){if(!m_path.isEmpty()&&!busy())drain([this]{send("history",{{"path",m_path}});});}
void NotesController::historyVersion(const QString &hash,bool restore){
    if(m_path.isEmpty()||busy()||(restore&&readOnly()))return;
    drain([this,hash,restore]{if(restore)m_organizing=true;send("historyVersion",{{"path",m_path},{"hash",hash},{"restore",restore}});emit changed();});
}
void NotesController::saveSmartFolder(const QString &name){if(!busy())send("smartSave",{{"name",name},{"query",m_query},{"filter",m_filter}});}
void NotesController::deleteSmartFolder(const QString &name){if(!busy())send("smartDelete",{{"name",name}});}
void NotesController::openSmartFolder(const QString &name){const auto rule=m_smartFolders.value(name).toMap();if(rule.isEmpty())return;setFilter(rule.value("filter","all").toString());setQuery(rule.value("query").toString());}
void NotesController::rebuildIndex(){if(!busy())send("reindex");}

void NotesController::inspectTemplates(){if(!m_library.isEmpty())send("templates");}
void NotesController::saveTemplate(const QString &name,const QString &text){if(!busy())send("templateSave",{{"name",name},{"text",text}});}
void NotesController::saveCurrentTemplate(const QString &name){if(!busy()&&!m_path.isEmpty())drain([this,name]{send("templateSave",{{"name",name},{"text",m_session->source()}});});}
void NotesController::deleteTemplate(const QString &name){if(!busy())send("templateDelete",{{"name",name}});}
void NotesController::createFromTemplate(const QString &name,const QString &title){if(!busy())drain([this,name,title]{m_organizing=true;send("fromTemplate",{{"name",name},{"title",title},{"folder",m_filter.startsWith("folder:")?m_filter.mid(7):QString()}});emit changed();});}
void NotesController::createDailyNote(){if(!busy())drain([this]{m_organizing=true;send("dailyNote");emit changed();});}

void NotesController::createMissingLink() {
    if(busy()||m_missingLink.value("path").toString()!=m_path||m_missingLink.isEmpty())return;
    const auto request=m_missingLink;
    drain([this,request]{m_organizing=true;send("createWiki",{{"path",m_path},{"target",request.value("target")},{"fragment",request.value("fragment")}});emit changed();});
}
void NotesController::editTags(const QString &action,const QStringList &paths,const QStringList &tags) {
    if(busy()||paths.isEmpty()||paths.size()>200||!QStringList{"add","remove"}.contains(action))return;
    drain([this,action,paths,tags]{
        QVariantList items;
        for(const auto &path:paths){QString hash;for(const auto &v:m_notes){auto n=v.toMap();if(n.value("path").toString()==path){hash=n.value("baseHash").toString();break;}}
            if(path==m_path)hash=m_base;items.append(QVariantMap{{"path",path},{"baseHash",hash}});}
        m_organizing=true;send("tagBatch",{{"action",action},{"items",items},{"tags",tags}});emit changed();
    });
}
void NotesController::previewTagRename(const QString &from,const QString &to) {
    if(busy())return;
    drain([this,from,to]{m_tagPlan.clear();send("tagPlan",{{"action","rename"},{"tags",QStringList{from.trimmed()}},{"replacement",to.trimmed()}});emit changed();});
}
void NotesController::confirmTagRename() {
    if(busy()||m_tagPlan.value("items").toList().isEmpty())return;
    const auto plan=m_tagPlan;
    drain([this,plan]{m_organizing=true;send("tagBatch",plan);m_tagPlan.clear();emit changed();});
}

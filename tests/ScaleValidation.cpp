// Manual VM-only scale validation. Not part of the routine CTest suite.
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <sys/resource.h>
#include "NotesRepository.h"
#include "../src/NotesListModel.h"
#include <QPersistentModelIndex>
#include <algorithm>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--model")) {
        QVariantList rows;for(int i=0;i<10000;++i)rows.append(QVariantMap{{"id",QString::number(i)},{"path",QString::number(i)+".md"}});
        NotesListModel model;QElapsedTimer timer;timer.start();model.replace(rows);const auto initial=timer.elapsed();
        QPersistentModelIndex persistent(model.index(123));const auto original=model.data(persistent,Qt::UserRole);
        std::reverse(rows.begin(),rows.end());timer.restart();model.replace(rows);const auto reordered=timer.elapsed();
        if(model.data(persistent,Qt::UserRole)!=original||persistent.row()!=9876)qFatal("persistent row lost on large reorder");
        qInfo()<<"10000 model rows, initial:"<<initial<<"ms, reverse:"<<reordered<<"ms";return 0;
    }
    QTemporaryDir library;
    if(!library.isValid())return 1;
    auto write=[](const QString &p,const QByteArray &bytes){QFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();};
    auto require=[](bool ok,const char *message){if(!ok)qFatal("%s",message);};
    QJsonObject report;
    for(int folder=0;folder<100;++folder)QDir(library.path()).mkdir(QString::number(folder));
    for(int i=0;i<10000;++i) {
        const QString path=QString::number(i/100)+"/note-"+QString::number(i)+".md";
        const auto text=QString("---\ntags: [work, tag%1]\nstatus: draft\n---\n# Note %2\n\n中文知识检索 %2\n[[note-%3]]\n%4\n")
            .arg(i%20).arg(i).arg((i+1)%10000).arg(QString(400,'x')).toUtf8();
        require(write(library.filePath(path),text),"fixture write failed");
    }
    QByteArray longText="# Long note\n\n";
    for(int i=0;i<16000;++i)longText+="Paragraph "+QByteArray::number(i)+" "+QByteArray(100,'x')+" 中文长文。\n\n";
    require(write(library.filePath("long.md"),longText),"long fixture write failed");
    report.insert("notes",10001);report.insert("longBytes",longText.size());
    NotesRepository repository;
    auto request=[&](const QString &op,const QVariantMap &args=QVariantMap{}){
        QSignalSpy spy(&repository,&NotesRepository::finished);auto id=repository.request(op,args);
        QElapsedTimer deadline;deadline.start();
        while(deadline.elapsed()<180000){for(const auto &v:spy)if(v[0].toULongLong()==id)return v[2].toMap();spy.wait(1000);}
        qFatal("request timeout: %s",qPrintable(op));return QVariantMap{};
    };
    QElapsedTimer tick;tick.start();qint64 maxGap=0;int ticks=0;QTimer heartbeat;
    QObject::connect(&heartbeat,&QTimer::timeout,[&]{maxGap=qMax(maxGap,tick.restart());++ticks;});heartbeat.start(16);
    auto measure=[&](const QString &name,auto action){QElapsedTimer timer;timer.start();action();report.insert(name,double(timer.elapsed()));qInfo().noquote()<<name<<timer.elapsed()<<"ms";};
    measure("initialScanMs",[&]{require(request("selectLibrary",{{"path",library.path()}}).value("ok").toBool(),"select failed");});
    measure("indexedQueries30Ms",[&]{for(int i=0;i<30;++i)require(request("search",{{"query","中文知识检索 9999"},{"indexed",true}}).value("notes").toList().size()==1,"query mismatch");});
    measure("backlinksMs",[&]{require(request("knowledge",{{"path","0/note-0.md"}}).value("backlinks").toList().size()==1,"backlinks mismatch");});
    measure("longReadMs",[&]{require(request("read",{{"path","long.md"}}).value("text").toString().contains("Paragraph 15999"),"long read mismatch");});
    QSignalSpy changes(&repository,&NotesRepository::libraryChanged);
    measure("burst1000ConvergeMs",[&]{
        for(int i=0;i<1000;++i)require(write(library.filePath(QString::number(i/100)+"/note-"+QString::number(i)+".md"),("# Changed "+QString::number(i)+"\n\nexternal-burst-marker\n").toUtf8()),"burst failed");
        QElapsedTimer deadline;deadline.start();bool converged=false;
        while(deadline.elapsed()<120000){if(changes.isEmpty())changes.wait(1000);auto result=request("search",{{"query","external-burst-marker"},{"indexed",true}});if(result.value("notes").toList().size()==1000){converged=true;break;}}
        require(converged,"external changes failed to converge");
    });
    report.insert("watchNotifications",changes.size());
    report.insert("workerWaitMainThreadMaxGapMs",double(maxGap));report.insert("heartbeatTicks",ticks);
    struct rusage usage{};getrusage(RUSAGE_SELF,&usage);report.insert("peakRssKiB",double(usage.ru_maxrss));
    report.insert("ok",true);const auto json=QJsonDocument(report).toJson();
    if(app.arguments().size()>1)require(write(app.arguments()[1],json),"report write failed");
    qInfo().noquote()<<json;
}

#pragma once
#include "LinkPaths.h"
#include "MarkdownFile.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QVariantMap>
#include <QSet>

namespace Knowledge {
inline QString scalar(QString s) {
    s=s.trimmed();if(s.startsWith('"')){auto a=QJsonDocument::fromJson(('['+s+']').toUtf8()).array();return a.size()==1?a[0].toString():s;}
    if(s.size()>1&&s.startsWith('\'')&&s.endsWith('\''))return s.mid(1,s.size()-2).replace("''","'");return s;
}
inline QVariantMap properties(const QString &text) {
    QVariantMap result;if(!text.startsWith("---\n"))return result;
    auto lines=text.split('\n');QString block;
    for(int i=1;i<lines.size();++i){const auto line=lines[i];if(line=="---"||line=="...")break;
        if(block=="tags"&&line.trimmed().startsWith("- ")){auto tags=result.value("tags").toStringList();tags.append(scalar(line.trimmed().mid(2)));result.insert("tags",tags);continue;}
        static const QRegularExpression fieldPattern("^([A-Za-z_][A-Za-z0-9_-]*):[ \\t]*(.*)$");auto m=fieldPattern.match(line);if(!m.hasMatch())continue;
        block=m.captured(1);const QString value=m.captured(2);
        if(block=="tags") {QStringList tags;if(value.trimmed().startsWith('[')){auto array=QJsonDocument::fromJson(value.toUtf8()).array();if(!array.isEmpty())for(auto v:array)tags.append(v.toString());else for(auto v:value.mid(1,value.size()-2).split(',',Qt::SkipEmptyParts))tags.append(scalar(v));}else if(!value.trimmed().isEmpty())tags=scalar(value).split(',',Qt::SkipEmptyParts);for(QString &t:tags)t=t.trimmed();tags.removeAll("");tags.removeDuplicates();result.insert(block,tags);}
        else if(!value.isEmpty()&&!value.startsWith('{')&&!value.startsWith('[')&&value!="|"&&value!=">")result.insert(block,scalar(value));
    }return result;
}
inline QString update(QString text,const QString &key,const QVariant &value,bool remove=false) {
    QString encoded;if(key=="tags")encoded=QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(value.toStringList())).toJson(QJsonDocument::Compact));
    else encoded="'"+QString(value.toString()).replace("'","''")+"'";
    auto lines=text.split('\n');if(!text.startsWith("---\n")){if(remove)return text;return "---\n"+key+": "+encoded+"\n---\n"+text;}
    int end=1;while(end<lines.size()&&lines[end]!="---"&&lines[end]!="...")++end;if(end==lines.size())return {};
    for(int i=1;i<end;++i)if(lines[i].startsWith(key+':')){
        int last=i+1;while(last<end&&(lines[last].startsWith(' ')||lines[last].startsWith('\t')||(key=="tags"&&lines[last].startsWith("- "))))++last;
        for(int j=last-1;j>=i;--j)lines.removeAt(j);if(!remove)lines.insert(i,key+": "+encoded);return lines.join('\n');
    }
    if(!remove)lines.insert(end,key+": "+encoded);return lines.join('\n');
}
inline QStringList grams(QString s) {s=s.toCaseFolded();QSet<QString> set;for(int i=0;i+2<s.size();++i)set.insert(s.mid(i,3));return set.values();}
struct Document {QString hash,text,title,folded;QVariantMap props;QStringList grams,names;QVariantList links,headings;};
class Index {
public:
 QHash<QString,Document> documents;QHash<QString,QSet<QString>> postings,namedPaths;quint64 updates=0;
 void remove(const QString &path){auto old=documents.take(path);for(const auto &name:old.names){namedPaths[name].remove(path);if(namedPaths[name].isEmpty())namedPaths.remove(name);}for(const auto &g:old.grams){postings[g].remove(path);if(postings[g].isEmpty())postings.remove(g);}}
 void put(const QString &path,const QString &text,const QString &hash,const QString &title){
  if(documents.contains(path)&&documents[path].hash==hash&&documents[path].title==title)return;remove(path);
  Document d;d.text=text;d.hash=hash;d.title=title;d.folded=(title+'\n'+text).toCaseFolded();d.props=properties(text);d.grams=grams(d.folded);d.names={QFileInfo(path).completeBaseName(),QFileInfo(path).fileName(),title};d.names.removeDuplicates();
  const QString masked=LinkPaths::masked(text);static const QRegularExpression headings("(?m)^ {0,3}#{1,6} +(.+?) *#* *$");auto hit=headings.globalMatch(masked);while(hit.hasNext()){auto m=hit.next();d.headings.append(QVariantMap{{"title",m.captured(1)},{"position",m.capturedStart()}});}
  QString copy=text;LinkPaths::rewrite(copy,[&](const QString &target,bool wiki){const int pos=masked.indexOf(target);d.links.append(QVariantMap{{"target",target},{"wiki",wiki},{"position",pos},{"context",text.mid(qMax(0,pos-40),160).simplified()}});return target;});
  documents.insert(path,d);for(const auto &name:d.names)namedPaths[name].insert(path);for(const auto &g:d.grams)postings[g].insert(path);++updates;
 }
 QSet<QString> candidates(const QString &query)const{auto gs=grams(query);if(gs.isEmpty())return QSet<QString>(documents.keyBegin(),documents.keyEnd());QSet<QString> result=postings.value(gs.first());for(const auto &g:gs)result.intersect(postings.value(g));return result;}
 QStringList resolve(const QString &source,QString target,bool wiki)const {
  QStringList result;
  if(!wiki){QString p=LinkPaths::resolve(source,target);if(documents.contains(p))result.append(p);return result;}
  target=QUrl::fromPercentEncoding(target.toUtf8()).trimmed();
  if(target.contains('/')){QString p=QDir::cleanPath(QFileInfo(source).path()+'/'+target);if(!p.endsWith(".md",Qt::CaseInsensitive))p+=".md";if(documents.contains(p))result.append(p);return result;}
  result=namedPaths.value(target).values();
  result.sort();return result;
 }
};
}

#pragma once
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <QRegularExpression>
#include <QUrl>
#include <functional>

// Preserve every byte outside URL spans. Mask code/front matter before collecting spans.
namespace LinkPaths {
inline QString masked(QString s) {
    auto hide = [&](int a, int b) { for (int i=a;i<b;++i) if (s[i]!='\n') s[i]=' '; };
    if (s.startsWith("---\n")) { auto end=QRegularExpression("(?m)^(?:---|\\.\\.\\.)[ \\t]*$").match(s,4); if(end.hasMatch()) hide(0,end.capturedEnd()); }
    static const QRegularExpression fences("(?m)^ {0,3}(`{3,}|~{3,})[^\\n]*\\n");
    int from=0;
    while (true) {
        auto m=fences.match(s,from); if(!m.hasMatch()) break;
        QString marker=m.captured(1);
        QRegularExpression end("(?m)^ {0,3}"+QRegularExpression::escape(QString(marker.size(),marker[0]))+QString(marker[0])+"* *$");
        auto e=end.match(s,m.capturedEnd()); int stop=e.hasMatch()?e.capturedEnd():s.size(); hide(m.capturedStart(),stop); from=stop;
    }
    static const QRegularExpression inlineCode("(`+)[^`]*?\\1|<!--[\\s\\S]*?-->");
    auto it=inlineCode.globalMatch(s); while(it.hasNext()){auto m=it.next();hide(m.capturedStart(),m.capturedEnd());}
    for(int i=0;i+1<s.size();++i)if(s[i]=='\\'){if(s[i+1]=='['||s[i+1]==']')s[i+1]=' ';++i;}
    return s;
}
inline bool rewrite(QString &text, const std::function<QString(const QString &, bool)> &map) {
    QString mask=masked(text);
    // HTML URLs need an HTML parser; refuse rather than alter unknown attributes.
    static const QRegularExpression htmlUrls("<(?:a|img|source)\\b",QRegularExpression::CaseInsensitiveOption);
    if (mask.contains(htmlUrls)) return false;
    struct Span {int start,length; bool wiki;}; QList<Span> spans;
    static const QRegularExpression wiki("\\[\\[([^\\]\\n|#]+)(?:[^\\]\\n]*)\\]\\]");
    auto wi=wiki.globalMatch(mask); while(wi.hasNext()){auto m=wi.next();spans.append({int(m.capturedStart(1)),int(m.capturedLength(1)),true});}
    static const QRegularExpression start("\\]\\(|(?m:^ {0,3}\\[[^]\\n]+\\]:[ \\t]*)");
    auto it=start.globalMatch(mask);
    while(it.hasNext()) {
        auto m=it.next(); int p=m.capturedEnd(); while(p<mask.size() && mask[p].isSpace() && mask[p]!='\n')++p;
        bool angle=p<mask.size()&&mask[p]=='<'; if(angle)++p; int begin=p,depth=0;
        for(;p<mask.size();++p){QChar c=mask[p]; if(c=='\\' && p+1<mask.size()){++p;continue;} if(angle?c=='>':(c.isSpace()||(c==')'&&depth==0)))break; if(c=='(')++depth; if(c==')')--depth;}
        if(p>begin) spans.append({begin,p-begin,false});
    }
    std::sort(spans.begin(),spans.end(),[](auto a,auto b){return a.start>b.start;});
    for(auto s:spans) text.replace(s.start,s.length,map(text.mid(s.start,s.length),s.wiki));
    return true;
}
inline QString resolve(const QString &source, QString url) {
    url.replace(QRegularExpression("\\\\([!\"#$%&'()*+,./:;<=>?@\\[\\]^_`{|}~-])"),"\\1");
    QUrl u(url); if(!u.isRelative() || url.startsWith('/') || url.startsWith('#') || u.path().isEmpty()) return {};
    return QDir::cleanPath(QFileInfo(source).path()+'/'+u.path());
}
}

#pragma once
#include <QObject>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QTextCursor>
#include <QRegularExpression>
class Highlight : public QSyntaxHighlighter {
public:
 bool live=true;
 explicit Highlight(QTextDocument *d):QSyntaxHighlighter(d){}
 void highlightBlock(const QString &s) override {
  if(!live) return;
  QTextCharFormat f;
  if(s.startsWith("#")) { f.setFontWeight(QFont::Bold); f.setFontPointSize(20); setFormat(0,s.size(),f); }
  QRegularExpression re("\\*\\*(.+?)\\*\\*");
  auto it=re.globalMatch(s);
  while(it.hasNext()){auto m=it.next(); QTextCharFormat b;b.setFontWeight(QFont::Bold);setFormat(m.capturedStart(),m.capturedLength(),b);}
 }
};
class Native : public QObject {
 Q_OBJECT
 QTextDocument *doc=nullptr; Highlight *high=nullptr;
public:
 using QObject::QObject;
 Q_INVOKABLE void attach(QQuickTextDocument *d, const QString &text) {doc=d->textDocument();doc->setPlainText(text);doc->setUndoRedoEnabled(true);high=new Highlight(doc);}
 Q_INVOKABLE QString source(){QString s=doc->toRawText();s.replace(QChar::ParagraphSeparator,'\n');return s;}
 Q_INVOKABLE void mode(bool live){high->live=live;high->rehighlight();}
 Q_INVOKABLE void command(QString name,int anchor,int head) {
  QTextCursor c(doc);c.setPosition(anchor);c.setPosition(head,QTextCursor::KeepAnchor);
  if(name=="undo")doc->undo(&c);
  else if(name=="redo")doc->redo(&c);
  else {
   QString m=name=="bold"?"**":name=="italic"?"*":name=="strike"?"~~":"`";
   int lo=qMin(anchor,head),hi=qMax(anchor,head);
   c.beginEditBlock();c.setPosition(hi);c.insertText(m);c.setPosition(lo);c.insertText(m);c.endEditBlock();
   c.setPosition(anchor+m.size());c.setPosition(head+m.size(),QTextCursor::KeepAnchor);
  }
  emit selection(c.anchor(),c.position());
 }
signals:
 void selection(int anchor,int head);
};

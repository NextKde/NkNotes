#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFile>
#include <QJsonDocument>
#include <QFontInfo>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QQuickWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include "Native.h"
class Offline : public QWebEngineUrlRequestInterceptor {
public:
 using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
 void interceptRequest(QWebEngineUrlRequestInfo &i) override {
  const auto u=i.requestUrl();
  i.block(u.scheme()!="qrc" || !u.path().startsWith("/editor/"));
 }
};
int main(int argc,char **argv){
 QtWebEngineQuick::initialize();
 QGuiApplication app(argc,argv);
 Offline offline;
 QQuickWebEngineProfile::defaultProfile()->setUrlRequestInterceptor(&offline);
 QFile f(":/editor/fixture.md");f.open(QIODevice::ReadOnly);
 QFile commands(":/editor/commands.json");commands.open(QIODevice::ReadOnly);
 Native native;QQmlApplicationEngine e;
 e.rootContext()->setContextProperty("commandDefinitions",QJsonDocument::fromJson(commands.readAll()).toVariant());
 e.rootContext()->setContextProperty("nativeSession",&native);
 e.rootContext()->setContextProperty("hostFontPixelSize",QFontInfo(app.font()).pixelSize());
 const auto fixtureBytes=f.readAll();
 e.rootContext()->setContextProperty("fixture",QString::fromUtf8(fixtureBytes));
 e.rootContext()->setContextProperty("fixtureUtf8Bytes",fixtureBytes.size());
 e.loadFromModule("EditorComparison","Main");
 if(e.rootObjects().isEmpty())return 1;
 return app.exec();
}

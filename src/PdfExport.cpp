#include "NotesController.h"
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include <QTemporaryDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QTimer>
#include <QPageLayout>
#include <QPageSize>
#include <functional>

namespace {
class ExportRequests final : public QWebEngineUrlRequestInterceptor {
public:
    QUrl document;
    void interceptRequest(QWebEngineUrlRequestInfo &request) override {
        const bool embedded = request.requestUrl().scheme() == "data" &&
            (request.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeImage ||
             request.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeFontResource);
        request.block(request.requestUrl() != document && !embedded);
    }
};
class PdfJob final : public QObject {
public:
    QTemporaryDir folder;
    ExportRequests requests;
    QWebEngineProfile profile;
    QWebEnginePage page{&profile};
    QTimer deadline;
    QTimer readiness;
    std::function<void(bool)> finished;
    bool polling = false, done = false;
    explicit PdfJob(QObject *parent) : QObject(parent) {
        profile.setUrlRequestInterceptor(&requests);
        deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, this, [this] { finish(false); });
    }
    ~PdfJob() override { done = true; profile.setUrlRequestInterceptor(nullptr); }
    void finish(bool success) {
        if (done) return;
        done = true; deadline.stop(); readiness.stop(); finished(success); deleteLater();
    }
    void start(const QString &html, const QString &output) {
        const QString path = folder.path() + "/document.html";
        QFile file(path); const QByteArray bytes = html.toUtf8();
        if (!folder.isValid() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) { finish(false); return; }
        file.close(); requests.document = QUrl::fromLocalFile(path);
        connect(&page, &QWebEnginePage::loadFinished, this, [this](bool ok) { if (ok) readiness.start(40); else finish(false); });
        connect(&readiness, &QTimer::timeout, this, [this, output] {
            if (polling) return;
            polling = true;
            page.runJavaScript("document.fonts.status==='loaded' && Array.from(document.images).filter(i=>i.getAttribute('src')).every(i=>i.complete && i.naturalWidth>0)", [this, output](const QVariant &ready) {
                polling = false;
                if (done || !ready.toBool()) return;
                readiness.stop();
                page.printToPdf([this, output](const QByteArray &pdf) {
                    if (done) return;
                    QSaveFile target(output); target.setDirectWriteFallback(false);
                    const bool ok = !pdf.isEmpty() && !QFileInfo(output).isSymLink() && target.open(QIODevice::WriteOnly) && target.write(pdf) == pdf.size() && target.commit();
                    finish(ok);
                }, QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(17,16,17,18), QPageLayout::Millimeter));
            });
        });
        deadline.start(30000); page.load(requests.document);
    }
};
}
void NotesController::exportPdf(const QString &path, const QString &body, const QString &title, bool printing) {
    const QString output = outputPath(QUrl::fromLocalFile(path), "pdf");
    const QString document = exportDocument(body, title);
    if (output.isEmpty() || document.isEmpty()) { exportFinished(false, path, printing); emit pdfExportFinished(); return; }
    auto *job = new PdfJob(this);
    job->finished = [this, path, printing](bool ok) { exportFinished(ok, path, printing); emit pdfExportFinished(); };
    job->start(document, output);
}

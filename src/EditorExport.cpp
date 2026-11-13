#include "NotesController.h"
#include "storage/ManagedAssets.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUuid>

void NotesController::setEditorExporting(bool value) { if (m_exporting != value) { m_exporting = value; emit changed(); } }

QString NotesController::outputPath(const QUrl &target, const QString &extension) const {
    if (!target.isLocalFile() || (extension != "html" && extension != "pdf")) return {};
    QString file = target.toLocalFile();
    if (!file.endsWith('.' + extension, Qt::CaseInsensitive)) {
        if (systemFileDialogs()) return {}; // A portal grant covers the exact chosen name.
        file += '.' + extension;
    }
    if (QFileInfo(file).isSymLink()) return {};
    return file;
}
QString NotesController::printPreviewPath() const {
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/print";
    if (!QDir().mkpath(folder)) return {};
    return folder + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".pdf";
}
void NotesController::exportFinished(bool success, const QString &path, bool printPreview) {
    if (!success) m_status = "导出未完成，请检查输出路径和目录权限。正文仍保留。";
    else {
        m_status = "已导出：" + path;
        if (printPreview) {
            const QString folder = QFileInfo(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/print").canonicalFilePath();
            const QString file = QFileInfo(path).canonicalFilePath();
            if (!folder.isEmpty() && file.startsWith(folder + '/')) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(file));
                m_status = "已打开 PDF 打印预览，可在查看器中打印";
            }
        }
    }
    emit changed();
}
bool NotesController::exportHtml(const QUrl &target, const QString &body, const QString &title) {
    const QString path = outputPath(target, "html");
    if (path.isEmpty()) { exportFinished(false, path); return false; }
    const QString document = exportDocument(body, title);
    if (document.isEmpty()) { exportFinished(false, path); return false; }
    QSaveFile file(path); file.setDirectWriteFallback(false);
    const QByteArray bytes = document.toUtf8();
    const bool ok = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    exportFinished(ok, path); return ok;
}

QString NotesController::exportDocument(const QString &body, const QString &title) const {
    auto read = [](const QString &name) { QFile f(name); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
    QString css = QString::fromUtf8(read(":/editor/editor.css"));
    QString mathCss = QString::fromUtf8(read(":/editor/vendor/katex/katex.min.css"));
    const QRegularExpression urls("url\\((?:[\"']?)(fonts/[^)\"']+)(?:[\"']?)\\)");
    auto fonts = urls.globalMatch(mathCss);
    while (fonts.hasNext()) {
        const auto match = fonts.next();
        const QByteArray bytes = read(":/editor/vendor/katex/" + match.captured(1));
        if (bytes.isEmpty()) continue;
        const QString mime = match.captured(1).endsWith("woff2") ? "font/woff2" : match.captured(1).endsWith("woff") ? "font/woff" : "font/ttf";
        mathCss.replace(match.captured(), "url(data:" + mime + ";base64," + QString::fromLatin1(bytes.toBase64()) + ')');
    }
    QString html = body;
    const QRegularExpression images("<img\\b[^>]*\\bsrc=\"([^\"]+)\"[^>]*>", QRegularExpression::CaseInsensitiveOption);
    auto matches = images.globalMatch(body);
    const ManagedAssets::Context assets{assetRoot(), documentUrl(), assetManifest(), managedAssets()};
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString encoded = match.captured(1);
        const QUrl url(QString(encoded).replace("&amp;", "&"));
        if (url.scheme() == "data") {
            if (!url.toString().startsWith("data:image/", Qt::CaseInsensitive)) return {};
            continue;
        }
        QByteArray bytes;
        if (ManagedAssets::resolvePath(assets, url, &bytes).isEmpty()) return {};
        const QString mime = QMimeDatabase().mimeTypeForData(bytes).name();
        if (!mime.startsWith("image/")) return {};
        const QString data = "data:" + mime + ";base64," + QString::fromLatin1(bytes.toBase64());
        QString image = match.captured(); image.replace("src=\"" + encoded + "\"", "src=\"" + data + "\"");
        html.replace(match.captured(), image);
    }
    return  "<!doctype html><html lang=\"zh-CN\"><meta charset=\"utf-8\"><meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'; img-src data:; font-src data:\"><title>" + title.toHtmlEscaped() + "</title><style>" + css + mathCss + "</style><body class=\"exported\"><main id=\"editor\">" + html + "</main></body></html>";
}

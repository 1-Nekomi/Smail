#include "smail/ReplyBodyEditor.h"
#include "smail/ReplyMessage.h"
#include <QImageReader>
#include <QMimeData>
#include <QTextBlock>
#include <QTextImageFormat>
#include <QRegularExpression>
#include <QUuid>

namespace smail {
ReplyBodyEditor::ReplyBodyEditor(QWidget *parent) : QTextEdit(parent) { setAcceptRichText(false); }
void ReplyBodyEditor::insertFromMimeData(const QMimeData *source) {
    // External HTML and remote/file image URLs are never imported implicitly.
    if (source->hasText()) insertPlainText(source->text());
}
void ReplyBodyEditor::insertImageReference(const QString &id, const QString &path) {
    QImage image;
    if (validateReplyImage(path).isEmpty()) {
        QImageReader reader(path); reader.setAutoTransform(true);
        image = reader.read();
    }
    if (image.isNull()) {
        // Retain the reference visibly so saving cannot silently drop a missing image.
        textCursor().insertText(replyImageMarker(id));
        return;
    }
    const auto name = "smail-image:" + id;
    const auto size = image.size().scaled(280, 120, Qt::KeepAspectRatio);
    document()->addResource(QTextDocument::ImageResource, QUrl(name), image);
    QTextImageFormat format; format.setName(name); format.setWidth(size.width()); format.setHeight(size.height());
    format.setToolTip(path);
    auto cursor = textCursor(); cursor.insertImage(format); setTextCursor(cursor);
}
void ReplyBodyEditor::insertLocalImage(const QString &path) {
    auto error = validateReplyImage(path);
    if (!error.isEmpty()) fail(error);
    auto current = imagePaths().values(); current << path;
    error = validateReplyFiles(current);
    if (!error.isEmpty()) fail(error);
    QImageReader reader(path);
    if (reader.read().isNull()) fail("无法解码正文图片：" + path);
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    paths[id] = path; insertImageReference(id, path);
}
void ReplyBodyEditor::setBody(const QString &body, const QMap<QString, QString> &images) {
    clear(); paths = images;
    static const QRegularExpression marker(R"(\[\[image:([a-zA-Z0-9-]+)\]\])");
    auto matches = marker.globalMatch(body);
    qsizetype offset = 0;
    while (matches.hasNext()) {
        auto match = matches.next();
        insertPlainText(body.mid(offset, match.capturedStart() - offset));
        insertImageReference(match.captured(1), paths.value(match.captured(1)));
        offset = match.capturedEnd();
    }
    insertPlainText(body.mid(offset));
    document()->clearUndoRedoStacks();
    moveCursor(QTextCursor::Start);
}
QString ReplyBodyEditor::bodyText() const {
    QString result;
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        if (!result.isEmpty() || block != document()->begin()) result += '\n';
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (fragment.charFormat().isImageFormat())
                result += replyImageMarker(fragment.charFormat().toImageFormat().name().mid(QString("smail-image:").size())).repeated(fragment.length());
            else result += fragment.text();
        }
    }
    return result;
}
QMap<QString, QString> ReplyBodyEditor::imagePaths() const {
    QMap<QString, QString> result;
    const auto body = bodyText();
    for (auto it = paths.begin(); it != paths.end(); ++it)
        if (body.contains(replyImageMarker(it.key()))) result[it.key()] = it.value();
    return result;
}
}

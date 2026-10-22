#pragma once
#include <QTextEdit>
#include <QMap>

namespace smail {
class ReplyBodyEditor : public QTextEdit {
public:
    explicit ReplyBodyEditor(QWidget *parent = nullptr);
    void setBody(const QString &body, const QMap<QString, QString> &images);
    QString bodyText() const;
    QMap<QString, QString> imagePaths() const;
    void insertLocalImage(const QString &path);
protected:
    void insertFromMimeData(const QMimeData *source) override;
private:
    QMap<QString, QString> paths;
    void insertImageReference(const QString &id, const QString &path);
};
}

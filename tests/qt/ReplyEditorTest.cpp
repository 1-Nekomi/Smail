#include "smail/ReplyBodyEditor.h"
#include "smail/ReplyMessage.h"
#include "AnimatedGif.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextBlock>

class ReplyEditorTest : public QObject {
    Q_OBJECT
private slots:
    void insertionPersistenceDeletionAndUndo() {
        QTemporaryDir temp;
        QFile file(temp.filePath("动画.gif")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(animatedGif()); file.close();
        smail::ReplyBodyEditor editor;
        editor.setBody("前文\n后文", {});
        auto cursor = editor.textCursor(); cursor.setPosition(3); editor.setTextCursor(cursor);
        editor.insertLocalImage(file.fileName());
        const auto images = editor.imagePaths(); QCOMPARE(images.size(), 1);
        const auto marker = smail::replyImageMarker(images.firstKey());
        QCOMPARE(editor.bodyText(), "前文\n" + marker + "后文");
        smail::ReplyBodyEditor reopened;
        reopened.setBody(editor.bodyText(), images);
        QCOMPARE(reopened.bodyText(), editor.bodyText()); QCOMPARE(reopened.imagePaths(), images);
        cursor = editor.textCursor(); cursor.deletePreviousChar();
        QCOMPARE(editor.bodyText(), QString("前文\n后文")); QVERIFY(editor.imagePaths().isEmpty());
        editor.undo(); QCOMPARE(editor.imagePaths(), images);
        reopened.setBody(marker + marker, images); QCOMPARE(reopened.bodyText(), marker + marker);
        reopened.setBody("\nfirst\n\nlast\n", {}); QCOMPARE(reopened.bodyText(), QString("\nfirst\n\nlast\n"));
        const auto missing = temp.filePath("missing.gif");
        reopened.setBody(marker, {{images.firstKey(), missing}});
        QCOMPARE(reopened.bodyText(), marker); QCOMPARE(reopened.imagePaths().first(), missing);
    }
};
QTEST_MAIN(ReplyEditorTest)
#include "ReplyEditorTest.moc"

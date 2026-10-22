#include "smail/MainWindow.h"
#include <QApplication>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>
#include <QCommandLineParser>
#include <QSslSocket>
#include <QTabWidget>

int main(int argc, char **argv) {
    QApplication app(argc,argv);
    QApplication::setOrganizationName("Smail");
    QApplication::setApplicationName("Smail");
    QApplication::setApplicationVersion("0.1.6");
    QCommandLineParser parser;
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"data-dir", "Override local configuration/state directory.", "directory"});
    parser.addOption({"smoke-test", "Show the UI for one second, then exit without connecting."});
    parser.addOption({"screenshot", "Save an offline UI preview PNG and exit.", "file"});
    parser.process(app);
    QString path = parser.isSet("data-dir") ? QDir(parser.value("data-dir")).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(path)) return 1;
    QLockFile lock(path+"/app.lock");
    if (!lock.tryLock(0)) { QMessageBox::information(nullptr,"Smail","程序已运行，请查看系统托盘。"); return 1; }
    smail::MainWindow window(path); window.show();
    if (parser.isSet("smoke-test") || parser.isSet("screenshot"))
        QTimer::singleShot(1000,&app,[&] {
            if (!QSslSocket::supportsSsl()) { app.exit(3); return; }
            if (parser.isSet("screenshot")) {
                QString filename = parser.value("screenshot");
                if (!window.grab().save(filename)) { app.exit(2); return; }
                auto *tabs = window.findChild<QTabWidget *>();
                for (int i = 1; tabs && i < tabs->count(); ++i) {
                    tabs->setCurrentIndex(i);
                    app.processEvents();
                    QString page = filename;
                    page.insert(page.lastIndexOf('.'), QString("-%1").arg(i+1));
                    if (!window.grab().save(page)) { app.exit(2); return; }
                }
            }
            app.quit();
        });
    return app.exec();
}

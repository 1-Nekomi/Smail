#include "smail/MainWindow.h"
#include "smail/Collector.h"
#include "smail/Store.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace smail {
namespace {
QSpinBox *spin(int low, int high) { auto *s = new QSpinBox; s->setRange(low,high); return s; }
QStringList tokens(QString value) {
    value = value.toLower().replace(QStringLiteral("，"), ",");
    QStringList out;
    for (auto item : value.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts)) {
        if (item.startsWith('.')) item.remove(0,1);
        if (!out.contains(item)) out << item;
    }
    return out;
}
}
MainWindow::MainWindow(QString dir) : dataDir(std::move(dir)) {
    setWindowTitle("Smail · 邮件附件收取"); resize(840, 760);
    setWindowIcon(style()->standardIcon(QStyle::SP_DriveNetIcon));
    auto *central = new QWidget; auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(24,20,24,20); layout->setSpacing(14);
    auto *title = new QLabel("Smail  附件收件箱");
    title->setStyleSheet("font-size:24px;font-weight:600;color:#173753;");
    layout->addWidget(title);
    layout->addWidget(new QLabel("自动收取 · 按类型归档 · 每封可追溯"));
    tabs = new QTabWidget; layout->addWidget(tabs,1);
    auto *account = new QWidget; auto *form = new QFormLayout(account);
    form->setContentsMargins(20,20,20,20); form->setSpacing(14);
    provider = new QComboBox; provider->addItems({"QQ 邮箱", "自定义 IMAP / SMTP"});
    email = new QLineEdit; email->setPlaceholderText("12345678@qq.com");
    secret = new QLineEdit; secret->setEchoMode(QLineEdit::Password); secret->setPlaceholderText("邮箱授权码，仅在本次运行中保存");
    imap = new QLineEdit; smtp = new QLineEdit; imapPort = spin(1,65535); smtpPort = spin(1,65535);
    output = new QLineEdit; auto *browse = new QPushButton("选择目录");
    auto *folderRow = new QWidget; auto *folderLayout = new QHBoxLayout(folderRow);
    folderLayout->setContentsMargins(0,0,0,0); folderLayout->addWidget(output); folderLayout->addWidget(browse);
    interval = spin(15,3600); interval->setSuffix(" 秒");
    existing = new QCheckBox("首次连接时也处理历史邮件（包括已读）");
    form->addRow("邮箱类型",provider); form->addRow("邮箱地址",email); form->addRow("授权码",secret);
    form->addRow("IMAP 服务器",imap); form->addRow("IMAP TLS 端口",imapPort);
    form->addRow("SMTP 服务器",smtp); form->addRow("SMTP TLS 端口",smtpPort);
    form->addRow("附件保存位置",folderRow); form->addRow("检查间隔",interval); form->addRow(existing);
    auto *hint = new QLabel("QQ 邮箱：请在网页版设置中开启 IMAP/SMTP 并生成授权码。\n默认仅收取首次成功连接之后的新邮件；电脑需保持运行和联网。");
    hint->setWordWrap(true); form->addRow(hint); tabs->addTab(account,"1  邮箱设置");
    auto *rules = new QWidget; auto *rf = new QFormLayout(rules); rf->setContentsMargins(20,20,20,20); rf->setSpacing(14);
    extensions = new QLineEdit; extensions->setPlaceholderText("pdf, docx, jpg（留空允许全部扩展名）");
    mimeTypes = new QLineEdit; mimeTypes->setPlaceholderText("可选，如 image/*, application/pdf");
    auto *presets = new QWidget; auto *pl = new QHBoxLayout(presets); pl->setContentsMargins(0,0,0,0);
    for (auto label : {"图片", "文档", "图片 + 文档"}) {
        auto *button = new QPushButton(QString::fromUtf8(label)); pl->addWidget(button);
        connect(button,&QPushButton::clicked,this,[this,label] {
            QString images = "jpg,jpeg,png,gif,bmp,webp,tif,tiff";
            QString docs = "pdf,doc,docx,xls,xlsx,ppt,pptx,txt,csv,odt,ods";
            extensions->setText(QString::fromUtf8(label) == "图片" ? images : QString::fromUtf8(label) == "文档" ? docs : images+","+docs);
        });
    }
    strict = new QCheckBox("任一附件不符就整封拒收（默认仅保存匹配部分）");
    inlineImages = new QCheckBox("同时收取正文内嵌图片（可能包含签名图标）");
    attachmentSize = spin(1,200); attachmentSize->setSuffix(" MB");
    messageSize = spin(1,200); messageSize->setSuffix(" MB");
    reply = new QCheckBox("拒收时自动向发件人回复");
    rejection = new QPlainTextEdit; rejection->setMaximumHeight(120);
    rf->addRow("快捷选择",presets); rf->addRow("扩展名白名单",extensions); rf->addRow("MIME 白名单",mimeTypes);
    rf->addRow(strict); rf->addRow(inlineImages); rf->addRow("单附件上限",attachmentSize); rf->addRow("整封邮件上限",messageSize);
    rf->addRow(reply); rf->addRow("回复正文",rejection);
    auto *ruleHint = new QLabel("可用字段：{reason}、{allowed_types}\n拒收不会删除原邮件。无附件、自动邮件、列表邮件及超大邮件不自动回复。");
    ruleHint->setWordWrap(true); rf->addRow(ruleHint); tabs->addTab(rules,"2  收取规则");
    auto *records = new QWidget; auto *rl = new QVBoxLayout(records);
    auto *refresh = new QPushButton("刷新处理历史"); rl->addWidget(refresh);
    history = new QPlainTextEdit; history->setReadOnly(true); rl->addWidget(history,2);
    rl->addWidget(new QLabel("运行日志")); log = new QPlainTextEdit; log->setReadOnly(true); log->setMaximumBlockCount(500); rl->addWidget(log,1);
    tabs->addTab(records,"3  运行记录");
    auto *buttons = new QHBoxLayout;
    status = new QLabel("尚未启动"); buttons->addWidget(status,1);
    auto *open = new QPushButton("打开附件目录"); start = new QPushButton("开始收取"); stop = new QPushButton("停止");
    start->setStyleSheet("background:#246bce;color:white;font-weight:600;padding:9px 22px;border-radius:5px;");
    stop->setEnabled(false); buttons->addWidget(open); buttons->addWidget(start); buttons->addWidget(stop); layout->addLayout(buttons);
    setCentralWidget(central);
    tray = new QSystemTrayIcon(windowIcon(),this); tray->setToolTip("Smail 邮件附件收取");
    auto *menu = new QMenu(this);
    menu->addAction("显示窗口",this,[this] { showNormal(); raise(); activateWindow(); });
    menu->addAction("退出",qApp,&QApplication::quit); tray->setContextMenu(menu);
    connect(tray,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) { showNormal(); raise(); activateWindow(); }
    });
    if (QSystemTrayIcon::isSystemTrayAvailable()) tray->show();
    connect(browse,&QPushButton::clicked,this,[this] { auto path = QFileDialog::getExistingDirectory(this,"附件保存位置",output->text()); if (!path.isEmpty()) output->setText(path); });
    connect(provider,&QComboBox::currentIndexChanged,this,[this](int index) {
        if (!index) { imap->setText("imap.qq.com"); smtp->setText("smtp.qq.com"); imapPort->setValue(993); smtpPort->setValue(465); }
    });
    connect(open,&QPushButton::clicked,this,[this] {
        if (!output->text().isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(output->text()));
    });
    connect(refresh,&QPushButton::clicked,this,&MainWindow::refreshHistory);
    connect(start,&QPushButton::clicked,this,&MainWindow::begin);
    connect(stop,&QPushButton::clicked,this,[this] { if (collector) { collector->requestInterruption(); stop->setEnabled(false); status->setText("正在停止…"); } });
    connect(reply,&QCheckBox::toggled,rejection,&QWidget::setEnabled);
    try { fill(Config::load(dataDir+"/settings.json")); }
    catch (const std::exception &e) { fill(Config{}); appendLog(QString::fromUtf8(e.what())); }
    refreshHistory();
}
MainWindow::~MainWindow() { if (collector) { collector->requestInterruption(); collector->wait(); } }
void MainWindow::fill(const Config &c) {
    provider->setCurrentIndex(c.imapHost == "imap.qq.com" ? 0 : 1);
    email->setText(c.email); imap->setText(c.imapHost); smtp->setText(c.smtpHost);
    imapPort->setValue(c.imapPort); smtpPort->setValue(c.smtpPort);
    output->setText(c.outputDir.isEmpty() ? QDir::homePath()+"/Downloads/Smail" : c.outputDir);
    interval->setValue(c.interval); extensions->setText(c.extensions.join(","));
    mimeTypes->setText(c.mimeTypes.join(",")); strict->setChecked(c.strict); inlineImages->setChecked(c.inlineImages);
    existing->setChecked(c.includeExisting); reply->setChecked(c.reply); rejection->setPlainText(c.rejection);
    rejection->setEnabled(c.reply); attachmentSize->setValue(c.maxAttachmentMb); messageSize->setValue(c.maxMessageMb);
}
Config MainWindow::readConfig() const {
    Config c; c.email = email->text().trimmed(); c.secret = secret->text();
    c.imapHost = imap->text().trimmed(); c.smtpHost = smtp->text().trimmed();
    c.imapPort = imapPort->value(); c.smtpPort = smtpPort->value(); c.outputDir = QDir(output->text().trimmed()).absolutePath();
    c.interval = interval->value(); c.extensions = tokens(extensions->text()); c.mimeTypes = tokens(mimeTypes->text());
    c.strict = strict->isChecked(); c.inlineImages = inlineImages->isChecked(); c.includeExisting = existing->isChecked();
    c.reply = reply->isChecked(); c.rejection = rejection->toPlainText();
    c.maxAttachmentMb = attachmentSize->value(); c.maxMessageMb = messageSize->value(); return c;
}
void MainWindow::begin() {
    if (collector) return;
    auto c = readConfig();
    auto error = c.validate();
    if (output->text().trimmed().isEmpty()) error = "请选择附件保存目录。";
    if (!error.isEmpty()) { QMessageBox::warning(this,"配置需要调整",error); return; }
    try {
        if (!QDir().mkpath(c.outputDir)) fail("无法创建附件目录");
        c.save(dataDir+"/settings.json");
    } catch (const std::exception &e) { QMessageBox::warning(this,"无法开始",QString::fromUtf8(e.what())); return; }
    collector = new Collector(c,dataDir+"/history.sqlite",this);
    connect(collector,&Collector::activity,this,&MainWindow::appendLog);
    connect(collector,&Collector::processed,this,[this](const QString &subject,const QString &detail) {
        appendLog(subject+" — "+detail); refreshHistory();
        if (tray->isVisible()) tray->showMessage("Smail · "+subject.left(80),detail,QSystemTrayIcon::Information,8000);
        QApplication::alert(this,5000);
    });
    connect(collector,&QThread::finished,this,[this] {
        collector->deleteLater(); collector = nullptr;
        start->setEnabled(true); stop->setEnabled(false); tabs->setTabEnabled(0,true); tabs->setTabEnabled(1,true);
        status->setText("已停止"); appendLog("收取已停止"); refreshHistory();
    });
    tabs->setTabEnabled(0,false); tabs->setTabEnabled(1,false); tabs->setCurrentIndex(2);
    start->setEnabled(false); stop->setEnabled(true); collector->start();
}
void MainWindow::appendLog(const QString &message) {
    QString clean = message; clean.replace('\r',' '); clean.replace('\n',' ');
    QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")+"  "+clean;
    log->appendPlainText(line); status->setText(clean.left(44)); status->setToolTip(clean);
    QString path = dataDir+"/smail.log";
    if (QFileInfo(path).size() > 2*1024*1024) { QFile::remove(path+".1"); QFile::rename(path,path+".1"); }
    QFile f(path); if (f.open(QIODevice::Append)) f.write((line+"\n").toUtf8());
}
void MainWindow::refreshHistory() {
    try { Store store(dataDir+"/history.sqlite"); history->setPlainText(store.history().join("\n\n")); }
    catch (const std::exception &e) { appendLog(QString::fromUtf8(e.what())); }
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (tray->isVisible()) { hide(); event->ignore(); tray->showMessage("Smail","程序仍在托盘运行。右键托盘图标可退出。"); }
    else { event->accept(); QApplication::quit(); }
}
}

#include "smail/MainWindow.h"
#include "smail/Collector.h"
#include "smail/Store.h"
#include "smail/CredentialStore.h"
#include "smail/MailClient.h"
#include "smail/ReplyMessage.h"
#include "smail/ReplyBodyEditor.h"
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
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QThread>
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
QStringList filePaths(QListWidget *list) {
    QStringList result;
    for (int i = 0; i < list->count(); ++i) result << list->item(i)->text();
    return result;
}
void addReplyEditor(QTabWidget *tabs, const QString &title, QCheckBox *enabled,
                    ReplyBodyEditor *body, QLineEdit *&subject, QListWidget *&files) {
    auto *page = new QWidget; auto *layout = new QVBoxLayout(page);
    layout->addWidget(enabled);
    auto *fields = new QWidget; auto *form = new QVBoxLayout(fields);
    form->setSizeConstraint(QLayout::SetMinimumSize);
    form->setContentsMargins(0,0,0,0);
    form->addWidget(new QLabel("回复主题")); subject = new QLineEdit;
    subject->setMaxLength(200); form->addWidget(subject);
    auto *bodyTools = new QHBoxLayout;
    bodyTools->addWidget(new QLabel("回复正文"));
    auto *insertImage = new QPushButton("插入正文图片…"); bodyTools->addWidget(insertImage); form->addLayout(bodyTools);
    body->setMinimumHeight(150); body->setMaximumHeight(180); form->addWidget(body);
    auto *imageHint = new QLabel("在光标位置插图，选中图片后按 Delete 删除。支持 PNG、JPEG、GIF；请保留原文件。");
    imageHint->setWordWrap(true); form->addWidget(imageHint);
    QObject::connect(insertImage, &QPushButton::clicked, page, [page, body] {
        const auto path = QFileDialog::getOpenFileName(page, "插入正文图片", {}, "图片 (*.png *.jpg *.jpeg *.gif)");
        if (path.isEmpty()) return;
        try { body->insertLocalImage(path); body->setFocus(); }
        catch (const std::exception &e) { QMessageBox::warning(page, "无法插入图片", QString::fromUtf8(e.what())); }
    });
    form->addWidget(new QLabel("回复附件（图片、文档等）")); files = new QListWidget;
    files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    files->setMinimumHeight(70); files->setMaximumHeight(100); form->addWidget(files);
    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton("添加文件…"); auto *remove = new QPushButton("移除选中");
    buttons->addWidget(add); buttons->addWidget(remove); form->addLayout(buttons);
    auto *hint = new QLabel("正文图片和附件合计最多 20 个、25 MB。此列表的文件作为普通附件发送；请保留原文件。");
    hint->setWordWrap(true); form->addWidget(hint);
    layout->addWidget(fields); layout->addStretch();
    fields->setEnabled(enabled->isChecked());
    QObject::connect(enabled, &QCheckBox::toggled, fields, &QWidget::setEnabled);
    QObject::connect(add, &QPushButton::clicked, page, [page, files] {
        const auto selected = QFileDialog::getOpenFileNames(page, "选择回复附件", {}, "所有文件 (*);;图片 (*.png *.jpg *.jpeg *.gif *.webp);;文档 (*.pdf *.docx *.xlsx *.pptx *.txt)");
        auto paths = filePaths(files);
        for (const auto &path : selected) if (!paths.contains(path)) paths << path;
        auto error = validateReplyFiles(paths);
        if (!error.isEmpty()) { QMessageBox::warning(page, "无法添加附件", error); return; }
        files->clear(); files->addItems(paths);
        for (int i = 0; i < files->count(); ++i) files->item(i)->setToolTip(files->item(i)->text());
    });
    QObject::connect(remove, &QPushButton::clicked, page, [files] {
        for (auto *item : files->selectedItems()) delete files->takeItem(files->row(item));
    });
    tabs->addTab(page, title);
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
    secret = new QLineEdit; secret->setEchoMode(QLineEdit::Password); secret->setPlaceholderText("输入邮箱授权码");
    rememberSecret = new QCheckBox("加密记住授权码（当前 Windows 用户）");
    rememberSecret->setEnabled(CredentialStore::supported());
    auto *saveSecret = new QPushButton("保存设置");
    auto *forgetSecret = new QPushButton("忘记授权码");
    auto *credentialRow = new QWidget; auto *credentialLayout = new QHBoxLayout(credentialRow);
    credentialLayout->setContentsMargins(0,0,0,0);
    credentialLayout->addWidget(rememberSecret,1);
    credentialLayout->addWidget(saveSecret); credentialLayout->addWidget(forgetSecret);
    imap = new QLineEdit; smtp = new QLineEdit; imapPort = spin(1,65535); smtpPort = spin(1,65535);
    output = new QLineEdit; auto *browse = new QPushButton("选择目录");
    auto *folderRow = new QWidget; auto *folderLayout = new QHBoxLayout(folderRow);
    folderLayout->setContentsMargins(0,0,0,0); folderLayout->addWidget(output); folderLayout->addWidget(browse);
    interval = spin(15,3600); interval->setSuffix(" 秒");
    existing = new QCheckBox("首次连接时也处理历史邮件（包括已读）");
    form->addRow("邮箱类型",provider); form->addRow("邮箱地址",email); form->addRow("授权码",secret);
    form->addRow(credentialRow);
    form->addRow("IMAP 服务器",imap); form->addRow("IMAP TLS 端口",imapPort);
    form->addRow("SMTP 服务器",smtp); form->addRow("SMTP TLS 端口",smtpPort);
    form->addRow("附件保存位置",folderRow); form->addRow("检查间隔",interval); form->addRow(existing);
    auto *hint = new QLabel("QQ 邮箱：请在网页版设置中开启 IMAP/SMTP 并生成授权码。\n每次开始收取会处理收件箱中未处理过的未读邮件，然后持续收取新邮件；电脑需保持运行和联网。");
    hint->setWordWrap(true); form->addRow(hint); tabs->addTab(account,"1  邮箱设置");
    auto *rules = new QWidget; auto *rulesLayout = new QHBoxLayout(rules);
    rulesLayout->setSizeConstraint(QLayout::SetMinimumSize);
    rulesLayout->setContentsMargins(16,16,16,16); rulesLayout->setSpacing(16);
    auto *filters = new QGroupBox("附件筛选"); auto *rf = new QFormLayout(filters); rf->setSpacing(14);
    rf->setRowWrapPolicy(QFormLayout::WrapAllRows);
    auto *responses = new QGroupBox("自动回复"); auto *responseLayout = new QVBoxLayout(responses);
    responseLayout->setSpacing(8);
    rulesLayout->addWidget(filters,1); rulesLayout->addWidget(responses,1);
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
    strict = new QCheckBox("任一附件不符就整封拒收");
    strict->setToolTip("未勾选时，仅保存匹配的附件。");
    inlineImages = new QCheckBox("同时收取正文内嵌图片");
    inlineImages->setToolTip("可能包含签名图标；未勾选时，只有内嵌图片的邮件视为无附件。");
    attachmentSize = spin(1,200); attachmentSize->setSuffix(" MB");
    messageSize = spin(1,200); messageSize->setSuffix(" MB");
    reply = new QCheckBox("拒收时自动向发件人回复");
    rejection = new ReplyBodyEditor;
    withAttachmentReply = new QCheckBox("有附件：收取成功后自动回复");
    withoutAttachmentReply = new QCheckBox("无附件：自动回复");
    withAttachmentMessage = new ReplyBodyEditor;
    withoutAttachmentMessage = new ReplyBodyEditor;
    rf->addRow("快捷选择",presets); rf->addRow("扩展名白名单",extensions); rf->addRow("MIME 白名单",mimeTypes);
    rf->addRow(strict); rf->addRow(inlineImages); rf->addRow("单附件上限",attachmentSize); rf->addRow("整封邮件上限",messageSize);
    auto *replyTabs = new QTabWidget; responseLayout->addWidget(replyTabs);
    addReplyEditor(replyTabs, "有附件", withAttachmentReply, withAttachmentMessage, withAttachmentSubject, withAttachmentFiles);
    addReplyEditor(replyTabs, "无附件", withoutAttachmentReply, withoutAttachmentMessage, withoutAttachmentSubject, withoutAttachmentFiles);
    addReplyEditor(replyTabs, "拒收", reply, rejection, rejectionSubject, rejectionFiles);
    auto *ruleHint = new QLabel("有附件仅在保存成功后回复；附件不合规则使用拒收回复。每封邮件最多自动回复一次。\n可用字段：{attachment_count}（已保存数量）、{reason}、{allowed_types}。");
    ruleHint->setWordWrap(true); responseLayout->addWidget(ruleHint);
    auto *safetyHint = new QLabel("自动邮件、列表邮件、自发邮件、no-reply 发件人及整封超限邮件不自动回复。内嵌图片按左侧选项判定。");
    safetyHint->setWordWrap(true); rf->addRow(safetyHint);
    auto *testSmtp = new QPushButton("测试 SMTP 发信连接");
    testSmtp->setToolTip("只连接并登录 SMTP，不发送邮件；用于检查网络或代理是否允许发信。");
    responseLayout->addWidget(testSmtp);
    auto *smtpHint = new QLabel("回复经 SMTP 发送。若测试失败，请检查 VPN/代理（如 Clash 全局模式）或防火墙是否封禁 465 端口。");
    smtpHint->setWordWrap(true); responseLayout->addWidget(smtpHint);
    auto *saveRules = new QPushButton("保存收取规则"); responseLayout->addWidget(saveRules);
    responseLayout->addStretch();
    auto *rulesScroll = new QScrollArea; rulesScroll->setWidgetResizable(true);
    rulesScroll->setFrameShape(QFrame::NoFrame); rulesScroll->setWidget(rules);
    tabs->addTab(rulesScroll,"2  收取规则");
    connect(saveRules,&QPushButton::clicked,saveSecret,&QPushButton::click);
    connect(testSmtp,&QPushButton::clicked,this,[this,testSmtp] {
        if (smtpProbe) return;
        auto c = readConfig();
        if (c.secret.isEmpty()) {
            try { c.secret = CredentialStore(dataDir+"/credentials").load(c); }
            catch (const std::exception &e) { QMessageBox::warning(this,"无法读取授权码",QString::fromUtf8(e.what())); return; }
        }
        auto error = c.validate();
        if (!error.isEmpty()) { QMessageBox::warning(this,"配置需要调整",error); return; }
        smtpProbeCancelled.store(false);
        testSmtp->setEnabled(false);
        appendLog(QString("正在测试 SMTP %1:%2（仅登录，不发送邮件）…").arg(c.smtpHost).arg(c.smtpPort));
        auto *thread = QThread::create([this,c] {
            QString result;
            try {
                MailClient::testSmtp(c, [this] { return smtpProbeCancelled.load(); });
                result = "SMTP 连接与登录成功（未发送邮件）。";
            } catch (const std::exception &e) {
                result = "SMTP 测试失败：" + QString::fromUtf8(e.what());
            }
            QMetaObject::invokeMethod(this, [this,result] {
                appendLog(result);
                QMessageBox::information(this,"SMTP 测试",result);
            }, Qt::QueuedConnection);
        });
        smtpProbe = thread;
        connect(thread,&QThread::finished,this,[this,thread,testSmtp] {
            if (smtpProbe == thread) smtpProbe = nullptr;
            testSmtp->setEnabled(true);
            thread->deleteLater();
        });
        thread->start();
    });
    auto *records = new QWidget; auto *rl = new QVBoxLayout(records);
    auto *refresh = new QPushButton("刷新处理历史"); rl->addWidget(refresh);
    auto *historyHint = new QLabel("每次开始收取会按当前规则回复尚未处理的未读邮件。有附件、无附件分别使用对应文字；已有处理记录不重复回复，未回复原因显示在记录中。");
    historyHint->setWordWrap(true); rl->addWidget(historyHint);
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
    connect(withAttachmentReply,&QCheckBox::toggled,withAttachmentMessage,&QWidget::setEnabled);
    connect(withoutAttachmentReply,&QCheckBox::toggled,withoutAttachmentMessage,&QWidget::setEnabled);
    try { fill(Config::load(dataDir+"/settings.json")); }
    catch (const std::exception &e) { fill(Config{}); appendLog(QString::fromUtf8(e.what())); }
    loadSecret();
    // Clear any credential before switching account/server, so it is never
    // accidentally submitted to a newly configured endpoint.
    for (auto *field : {email, imap, smtp})
        connect(field,&QLineEdit::textChanged,this,&MainWindow::loadSecret);
    for (auto *field : {imapPort, smtpPort})
        connect(field,&QSpinBox::valueChanged,this,&MainWindow::loadSecret);
    connect(saveSecret,&QPushButton::clicked,this,[this] {
        auto c = readConfig();
        auto error = c.validate();
        if (output->text().trimmed().isEmpty()) error = "请选择附件保存目录。";
        if (!error.isEmpty()) { QMessageBox::warning(this,"配置需要调整",error); return; }
        try {
            persistSettings(c);
            appendLog(c.rememberSecret ? "设置已保存，授权码已加密保存" : "设置已保存，授权码仅供本次使用");
        } catch (const std::exception &e) { QMessageBox::warning(this,"保存失败",QString::fromUtf8(e.what())); }
    });
    connect(forgetSecret,&QPushButton::clicked,this,[this] {
        try {
            CredentialStore(dataDir+"/credentials").remove(readConfig());
            secret->clear(); rememberSecret->setChecked(false);
            readConfig().save(dataDir+"/settings.json");
            appendLog("已删除当前邮箱的已保存授权码");
        } catch (const std::exception &e) { QMessageBox::warning(this,"操作失败",QString::fromUtf8(e.what())); }
    });
    refreshHistory();
}
MainWindow::~MainWindow() {
    if (collector) { collector->requestInterruption(); collector->wait(); }
    if (smtpProbe) { smtpProbeCancelled.store(true); smtpProbe->wait(); }
}
void MainWindow::fill(const Config &c) {
    provider->setCurrentIndex(c.imapHost == "imap.qq.com" ? 0 : 1);
    email->setText(c.email); imap->setText(c.imapHost); smtp->setText(c.smtpHost);
    imapPort->setValue(c.imapPort); smtpPort->setValue(c.smtpPort);
    output->setText(c.outputDir.isEmpty() ? QDir::homePath()+"/Downloads/Smail" : c.outputDir);
    interval->setValue(c.interval); extensions->setText(c.extensions.join(","));
    mimeTypes->setText(c.mimeTypes.join(",")); strict->setChecked(c.strict); inlineImages->setChecked(c.inlineImages);
    existing->setChecked(c.includeExisting); reply->setChecked(c.reply); rejection->setBody(c.rejection, c.rejectionImages);
    rejection->setEnabled(c.reply); attachmentSize->setValue(c.maxAttachmentMb); messageSize->setValue(c.maxMessageMb);
    rememberSecret->setChecked(CredentialStore::supported() && c.rememberSecret);
    withAttachmentReply->setChecked(c.withAttachmentReply);
    withoutAttachmentReply->setChecked(c.withoutAttachmentReply);
    withAttachmentMessage->setBody(c.withAttachmentMessage, c.withAttachmentImages);
    withoutAttachmentMessage->setBody(c.withoutAttachmentMessage, c.withoutAttachmentImages);
    withAttachmentMessage->setEnabled(c.withAttachmentReply);
    withoutAttachmentMessage->setEnabled(c.withoutAttachmentReply);
    withAttachmentSubject->setText(c.withAttachmentSubject);
    withoutAttachmentSubject->setText(c.withoutAttachmentSubject);
    rejectionSubject->setText(c.rejectionSubject);
    withAttachmentFiles->clear(); withAttachmentFiles->addItems(c.withAttachmentFiles);
    withoutAttachmentFiles->clear(); withoutAttachmentFiles->addItems(c.withoutAttachmentFiles);
    rejectionFiles->clear(); rejectionFiles->addItems(c.rejectionFiles);
}
Config MainWindow::readConfig() const {
    Config c; c.email = email->text().trimmed(); c.secret = secret->text();
    c.imapHost = imap->text().trimmed(); c.smtpHost = smtp->text().trimmed();
    c.imapPort = imapPort->value(); c.smtpPort = smtpPort->value(); c.outputDir = QDir(output->text().trimmed()).absolutePath();
    c.interval = interval->value(); c.extensions = tokens(extensions->text()); c.mimeTypes = tokens(mimeTypes->text());
    c.strict = strict->isChecked(); c.inlineImages = inlineImages->isChecked(); c.includeExisting = existing->isChecked();
    c.reply = reply->isChecked(); c.rejection = rejection->bodyText(); c.rejectionImages = rejection->imagePaths();
    c.rememberSecret = rememberSecret->isChecked();
    c.withAttachmentReply = withAttachmentReply->isChecked();
    c.withoutAttachmentReply = withoutAttachmentReply->isChecked();
    c.withAttachmentMessage = withAttachmentMessage->bodyText();
    c.withoutAttachmentMessage = withoutAttachmentMessage->bodyText();
    c.withAttachmentImages = withAttachmentMessage->imagePaths();
    c.withoutAttachmentImages = withoutAttachmentMessage->imagePaths();
    c.withAttachmentSubject = withAttachmentSubject->text();
    c.withoutAttachmentSubject = withoutAttachmentSubject->text();
    c.rejectionSubject = rejectionSubject->text();
    c.withAttachmentFiles = filePaths(withAttachmentFiles);
    c.withoutAttachmentFiles = filePaths(withoutAttachmentFiles);
    c.rejectionFiles = filePaths(rejectionFiles);
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
        persistSettings(c);
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
void MainWindow::loadSecret() {
    secret->clear();
    if (!rememberSecret->isChecked() || email->text().trimmed().isEmpty()) return;
    try { secret->setText(CredentialStore(dataDir+"/credentials").load(readConfig())); }
    catch (const std::exception &e) { appendLog(QString::fromUtf8(e.what())); }
}
void MainWindow::persistSettings(const Config &c) {
    CredentialStore credentials(dataDir+"/credentials");
    if (c.rememberSecret) credentials.save(c);
    else credentials.remove(c);
    c.save(dataDir+"/settings.json");
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

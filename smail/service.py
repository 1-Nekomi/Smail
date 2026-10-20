import contextlib
import hashlib
import imaplib
import json
import logging
import os
import re
import smtplib
import sqlite3
import ssl
import subprocess
from email import policy
from email.message import EmailMessage
from email.parser import BytesParser
from email.utils import formatdate, getaddresses, make_msgid
from pathlib import Path

log = logging.getLogger("smail")


@contextlib.contextmanager
def single_instance(directory):
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / "service.lock").open("a+b") as f:
        f.seek(0)
        f.write(b"0")
        f.flush()
        f.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as e:
            raise RuntimeError("同一状态目录已有程序运行") from e
        try:
            yield
        finally:
            f.seek(0)
            if os.name == "nt":
                msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(f.fileno(), fcntl.LOCK_UN)


class Store:
    def __init__(self, directory):
        directory.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(directory / "state.sqlite3")
        self.db.row_factory = sqlite3.Row
        self.db.executescript("""
            CREATE TABLE IF NOT EXISTS cursors (
              account TEXT PRIMARY KEY, validity TEXT, last_uid INTEGER);
            CREATE TABLE IF NOT EXISTS messages (
              key TEXT PRIMARY KEY, uid INTEGER, status TEXT, subject TEXT,
              reason TEXT, files TEXT, seen INTEGER DEFAULT 0,
              notified INTEGER DEFAULT 0, reply TEXT DEFAULT 'pending',
              email_notice TEXT DEFAULT 'pending',
              created TEXT DEFAULT CURRENT_TIMESTAMP);
        """)

    def row(self, key):
        return self.db.execute("SELECT * FROM messages WHERE key=?", (key,)).fetchone()

    def update(self, key, field, value):
        if field not in {"seen", "notified", "reply", "email_notice"}:
            raise ValueError(field)
        with self.db:
            self.db.execute(f"UPDATE messages SET {field}=? WHERE key=?", (value, key))

    def cursor(self, account, validity, uid):
        with self.db:
            self.db.execute("INSERT OR REPLACE INTO cursors VALUES(?,?,?)", (account, validity, uid))

    def save(self, key, uid, status, subject, reason, files):
        with self.db:
            self.db.execute(
                "INSERT INTO messages(key,uid,status,subject,reason,files) VALUES(?,?,?,?,?,?)",
                (key, uid, status, subject, reason, json.dumps(files, ensure_ascii=False)))


def checked(result):
    status, data = result
    if status != "OK":
        raise RuntimeError(f"IMAP 请求失败 ({status})")
    return data


def write_files(root, key, attachments):
    directory = root / hashlib.sha256(key.encode()).hexdigest()[:24]
    directory.mkdir(parents=True, exist_ok=True)
    paths = []
    for i, a in enumerate(attachments, 1):
        target = directory / f"{i:03d}_{a.name}"
        temp = target.with_name(target.name + ".part")
        with temp.open("wb") as f:
            f.write(a.content)
            f.flush()
            os.fsync(f.fileno())
        os.replace(temp, target)
        paths.append(str(target))
    return paths


def desktop_notice(body):
    if os.name != "nt":
        log.warning("当前系统不支持 Windows 桌面通知，已保留日志")
        return
    # User-controlled text is passed through environment, never shell source.
    script = """
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$tray = New-Object System.Windows.Forms.NotifyIcon
try {
  $tray.Icon = [System.Drawing.SystemIcons]::Information
  $tray.Visible = $true
  $tray.BalloonTipTitle = 'Smail'
  $tray.BalloonTipText = $env:SMAIL_NOTICE
  $tray.ShowBalloonTip(4000)
  Start-Sleep -Seconds 5
} finally { $tray.Dispose() }
"""
    subprocess.run(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden",
         "-Command", script],
        env=os.environ | {"SMAIL_NOTICE": body[:240]}, check=True, timeout=15,
        creationflags=subprocess.CREATE_NO_WINDOW,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def auto_message(message):
    return (message.get("Auto-Submitted", "no").lower() != "no"
            or bool(message.get("List-Id"))
            or message.get("Precedence", "").lower() in {"bulk", "list", "junk"}
            or bool(message.get("X-Smail-Generated"))
            or message.get("Return-Path", "").strip() == "<>")


class Collector:
    def __init__(self, c, store, password, imap_factory=None, smtp_factory=None):
        self.c, self.store, self.password = c, store, password
        self.imap_factory = imap_factory or imaplib.IMAP4_SSL
        self.smtp_factory = smtp_factory or smtplib.SMTP_SSL
        self.account = json.dumps([c["imap_host"], c["imap_port"], c["email"], c["folder"]])

    def send(self, recipient, subject, body, original=None):
        message = EmailMessage()
        message["From"] = self.c["email"]
        message["To"] = recipient
        message["Subject"] = subject[:200]
        message["Date"] = formatdate(localtime=True)
        message["Message-ID"] = make_msgid()
        message["Auto-Submitted"] = "auto-replied" if original else "auto-generated"
        message["X-Auto-Response-Suppress"] = "All"
        message["X-Smail-Generated"] = "yes"
        if original and original.get("Message-ID"):
            message["In-Reply-To"] = str(original["Message-ID"])
        message.set_content(body)
        with self.smtp_factory(self.c["smtp_host"], self.c["smtp_port"],
                               context=ssl.create_default_context(), timeout=30) as smtp:
            smtp.login(self.c["email"], self.password)
            refused = smtp.send_message(message)
            if refused:
                raise RuntimeError("SMTP 收件人被拒绝")

    def outbound(self, key, field, action):
        if self.store.row(key)[field] != "pending":
            return
        self.store.update(key, field, "attempted")
        try:
            action()
        except Exception:
            self.store.update(key, field, "uncertain")
            log.warning("邮件 %s 的 %s 发送失败或结果不确定，请人工核查；不自动重发", key, field)
        else:
            self.store.update(key, field, "sent")

    def finish(self, client, key, uid, message):
        row = self.store.row(key)
        if not row["seen"]:
            checked(client.uid("STORE", str(uid), "+FLAGS.SILENT", r"(\Seen)"))
            self.store.update(key, "seen", 1)
        body = (f"邮件 UID {uid}：{row['status']}\n主题：{row['subject']}\n"
                f"附件数：{len(json.loads(row['files']))}\n{row['reason']}")
        if not row["notified"]:
            log.info("%s；文件：%s", body.replace("\n", " | "), row["files"])
            if self.c["notification"] == "desktop":
                try:
                    desktop_notice(body)
                except Exception:
                    log.warning("桌面通知失败；请查看日志或 history")
            self.store.update(key, "notified", 1)
        if self.c["notification_email"] and not (message and message.get("X-Smail-Generated")):
            self.outbound(key, "email_notice", lambda: self.send(
                self.c["notification_email"], "Smail 邮件处理通知", body))
        else:
            self.store.update(key, "email_notice", "skipped")
        sender = getaddresses(message.get_all("From", [])) if message else []
        can_reply = (self.c["reply_enabled"] and row["status"] == "rejected"
                     and message is not None and not auto_message(message)
                     and len(sender) == 1 and "@" in sender[0][1]
                     and sender[0][1].casefold() != self.c["email"].casefold()
                     and not re.search(r"(mailer-daemon|postmaster|no-?reply)", sender[0][1], re.I))
        if can_reply:
            text = self.c["rejection_message"].format(
                reason=row["reason"], allowed_types=", ".join(self.c["allowed_extensions"]) or "不限")
            self.outbound(key, "reply", lambda: self.send(
                sender[0][1], "附件接收结果", text, message))
        elif row["reply"] == "pending":
            self.store.update(key, "reply", "skipped")

    def poll(self):
        from .attachments import select_attachments
        with self.imap_factory(self.c["imap_host"], self.c["imap_port"],
                               ssl_context=ssl.create_default_context(), timeout=30) as client:
            client.login(self.c["email"], self.password)
            folder = '"' + self.c["folder"].replace("\\", "\\\\").replace('"', '\\"') + '"'
            checked(client.select(folder))
            validity_data = client.response("UIDVALIDITY")[1]
            if not validity_data or not validity_data[0]:
                raise RuntimeError("服务器没有返回 UIDVALIDITY，不能安全去重")
            validity = validity_data[0].decode()
            previous = self.store.db.execute(
                "SELECT * FROM cursors WHERE account=?", (self.account,)).fetchone()
            fresh = not previous or previous["validity"] != validity
            last = 0 if fresh else previous["last_uid"]
            if previous and fresh:
                log.warning("UIDVALIDITY 已改变，重新建立邮箱基线")
            data = checked(client.uid("SEARCH", None, "ALL" if fresh else f"UID {last + 1}:*"))
            uids = sorted(int(x) for x in (data[0] or b"").split() if int(x) > last)
            if fresh and not self.c["include_existing"]:
                self.store.cursor(self.account, validity, max(uids, default=0))
                log.info("首次连接基线已建立，跳过 %d 封历史邮件", len(uids))
                return
            if fresh:
                self.store.cursor(self.account, validity, 0)
            for uid in uids:
                key = f"{self.account}:{validity}:{uid}"
                meta = checked(client.uid("FETCH", str(uid), "(RFC822.SIZE)"))
                # A message may be deleted between SEARCH and FETCH.
                raw_meta = b" ".join(x for x in meta if isinstance(x, bytes))
                match = re.search(rb"RFC822\.SIZE\s+(\d+)", raw_meta, re.I)
                if not match:
                    remaining = checked(client.uid("SEARCH", None, f"UID {uid}"))
                    if str(uid).encode() not in (remaining[0] or b"").split():
                        log.warning("UID %s 已从邮箱移除，跳过", uid)
                        self.store.cursor(self.account, validity, uid)
                        continue
                    raise RuntimeError(f"无法读取邮件 UID {uid} 的大小，下轮重试")
                message = None
                row = self.store.row(key)
                if int(match[1]) > self.c["max_message_mb"] * 1024 * 1024:
                    if not row:
                        self.store.save(key, uid, "rejected", "(邮件过大，未读取)",
                                        "整封邮件超过大小限制，不自动回复", [])
                else:
                    data = checked(client.uid("FETCH", str(uid), "(BODY.PEEK[])"))
                    payloads = [v[1] for v in data if isinstance(v, tuple) and isinstance(v[1], bytes)]
                    if len(payloads) != 1:
                        raise RuntimeError("邮件正文读取失败")
                    message = BytesParser(policy=policy.default).parsebytes(payloads[0])
                    if not row:
                        if message.get("X-Smail-Generated"):
                            status, attachments, reason = "ignored", [], "忽略 Smail 自动邮件"
                        else:
                            status, attachments, reason = select_attachments(message, self.c)
                        files = write_files(self.c["output_dir"], key, attachments) if attachments else []
                        subject = re.sub(r"[\r\n\x00-\x1f]", " ", str(message.get("Subject", "(无主题)")))
                        self.store.save(key, uid, status, subject[:500], reason, files)
                self.finish(client, key, uid, message)
                self.store.cursor(self.account, validity, uid)

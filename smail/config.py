import json
from pathlib import Path


def load(path):
    path = Path(path).resolve()
    c = json.loads(path.read_text(encoding="utf-8-sig"))
    defaults = dict(password_env="SMAIL_PASSWORD", imap_port=993, smtp_port=465,
                    folder="INBOX", output_dir="downloads", state_dir="data",
                    poll_seconds=60, include_existing=False, allowed_extensions=[],
                    allowed_mime_types=[], policy="matching", include_inline=False,
                    max_attachment_mb=25, max_message_mb=50, notification="log",
                    notification_email="", reply_enabled=False, rejection_message="附件未接收：{reason}")
    c = defaults | c
    for k in ("email", "imap_host", "password_env", "folder"):
        if not isinstance(c.get(k), str) or not c[k].strip():
            raise ValueError(f"必须配置 {k}")
        if any(ord(ch) < 32 for ch in c[k]):
            raise ValueError(f"{k} 不允许包含控制字符")
    for k in ("email", "notification_email"):
        value = c[k]
        if not isinstance(value, str) or (value and (
            value.count("@") != 1 or any(ch.isspace() for ch in value)
            or any(ch in value for ch in "<>,;"))):
            raise ValueError(f"{k} 必须是单个邮箱地址")
    if c["policy"] not in ("matching", "strict"):
        raise ValueError("policy 必须为 matching 或 strict")
    if c["notification"] not in ("desktop", "log"):
        raise ValueError("notification 必须为 desktop 或 log")
    for k in ("include_existing", "include_inline", "reply_enabled"):
        if not isinstance(c[k], bool):
            raise ValueError(f"{k} 必须是布尔值")
    for k in ("poll_seconds", "max_attachment_mb", "max_message_mb"):
        if isinstance(c[k], bool) or not isinstance(c[k], (int, float)) or not 0 < c[k] <= 1_000_000:
            raise ValueError(f"{k} 必须为有限正数")
    for k in ("imap_port", "smtp_port"):
        if type(c[k]) is not int or not 1 <= c[k] <= 65535:
            raise ValueError(f"{k} 端口无效")
    for k in ("allowed_extensions", "allowed_mime_types"):
        if not isinstance(c[k], list) or not all(isinstance(v, str) and v.strip() for v in c[k]):
            raise ValueError(f"{k} 必须为非空字符串组成的列表")
        c[k] = [v.lower().strip() for v in c[k]]
    c["allowed_extensions"] = [
        v if v.startswith(".") else "." + v for v in c["allowed_extensions"]
    ]
    for k in ("output_dir", "state_dir"):
        c[k] = (path.parent / c[k]).resolve()
    if (c["reply_enabled"] or c["notification_email"]) and not c.get("smtp_host"):
        raise ValueError("启用回复或邮件通知需要 smtp_host")
    if c["notification_email"] and c["notification_email"].casefold() == c["email"].casefold():
        raise ValueError("通知邮箱必须与收信邮箱不同")
    try:
        c["rejection_message"].format(reason="测试", allowed_types="测试")
    except (KeyError, IndexError, AttributeError, ValueError) as e:
        raise ValueError("rejection_message 只支持 {reason} 和 {allowed_types} 占位符") from e
    return c

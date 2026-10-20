import fnmatch
import mimetypes
import re
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Attachment:
    name: str
    content: bytes
    mime: str


def safe_name(name):
    name = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", name).strip(" .")
    if not name:
        name = "attachment"
    if name.split(".")[0].upper() in {"CON", "PRN", "AUX", "NUL", *(
        f"{p}{n}" for p in ("COM", "LPT") for n in range(1, 10)
    )}:
        name = "_" + name
    suffix = Path(name).suffix[:20]
    return name if len(name) <= 120 else name[:100] + suffix


def select_attachments(message, c):
    def parts(node):
        # Stop recursion at attached messages and multipart attachments.
        if node.get_content_disposition() == "attachment" or not node.is_multipart():
            yield node
        else:
            for child in node.iter_parts():
                yield from parts(child)

    accepted, rejected = [], []
    count = 0
    for part in parts(message):
        disposition = part.get_content_disposition()
        name = part.get_filename()
        inline = disposition == "inline" or (not name and part.get_content_maintype() == "image")
        if inline and not c["include_inline"]:
            continue
        if not (name or disposition == "attachment" or inline):
            continue
        # Treat attached messages as files; do not silently inspect their children.
        if part.is_multipart():
            if disposition == "attachment":
                rejected.append("不支持嵌套邮件或多部分附件")
                count += 1
            continue
        count += 1
        mime = part.get_content_type()
        name = safe_name(name or f"attachment-{count}{mimetypes.guess_extension(mime) or '.bin'}")
        content = part.get_payload(decode=True) or b""
        reason = None
        if c["allowed_extensions"] and Path(name).suffix.lower() not in c["allowed_extensions"]:
            reason = "扩展名不允许"
        elif c["allowed_mime_types"] and not any(fnmatch.fnmatchcase(mime, p) for p in c["allowed_mime_types"]):
            reason = "MIME 类型不允许"
        elif len(content) > c["max_attachment_mb"] * 1024 * 1024:
            reason = "附件超过大小限制"
        if reason:
            rejected.append(f"{name}：{reason}")
        else:
            accepted.append(Attachment(name, content, mime))
    if not count:
        return "ignored", [], "无可处理附件"
    if not accepted or (c["policy"] == "strict" and rejected):
        return "rejected", [], "；".join(rejected) or "没有符合规则的附件"
    return "accepted", accepted, "；".join(rejected)

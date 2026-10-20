import json
from email.message import EmailMessage
from pathlib import Path
import tempfile
import unittest

from smail.attachments import safe_name, select_attachments
from smail.config import load
from smail.service import Collector, Store, auto_message, write_files


def message():
    m = EmailMessage()
    m["From"] = "sender@example.com"
    m["Subject"] = "测试附件"
    m.set_content("正文")
    m.add_attachment(b"%PDF-test", maintype="application", subtype="pdf", filename="report.pdf")
    m.add_attachment(b"blocked", maintype="application", subtype="octet-stream", filename="run.exe")
    return m


class FakeIMAP:
    def __init__(self, payload):
        self.payload = payload
        self.ids = [1]
        self.fail_store = False
        self.marked = []
        self.fetches = 0

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass

    def login(self, *args):
        pass

    def select(self, *args):
        return "OK", [b"1"]

    def response(self, *args):
        return "UIDVALIDITY", [b"99"]

    def uid(self, command, *args):
        if command == "SEARCH":
            return "OK", [b" ".join(str(i).encode() for i in self.ids)]
        if command == "FETCH":
            if args[1] == "(RFC822.SIZE)":
                return "OK", [f"1 (RFC822.SIZE {len(self.payload)})".encode()]
            assert args[1] == "(BODY.PEEK[])"
            self.fetches += 1
            return "OK", [(b"1", self.payload)]
        if command == "STORE":
            if self.fail_store:
                raise OSError("network interrupted")
            self.marked.append(args[0])
            return "OK", []
        raise AssertionError(command)


class SmailTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        template = Path(__file__).resolve().parents[1] / "config.example.json"
        c = json.loads(template.read_text(encoding="utf-8"))
        c.update(notification="log", reply_enabled=False, include_existing=True)
        config = root / "config.json"
        config.write_text(json.dumps(c), encoding="utf-8")
        self.c = load(config)
        self.store = Store(self.c["state_dir"])

    def tearDown(self):
        self.store.db.close()
        self.temp.cleanup()

    def collector(self, fake):
        return Collector(self.c, self.store, "not-a-real-secret", imap_factory=lambda *a, **k: fake)

    def test_matching_and_strict(self):
        status, files, reason = select_attachments(message(), self.c)
        self.assertEqual(status, "accepted")
        self.assertEqual([f.name for f in files], ["report.pdf"])
        self.assertIn("run.exe", reason)
        self.c["policy"] = "strict"
        self.assertEqual(select_attachments(message(), self.c)[:2], ("rejected", []))

    def test_mime_and_size(self):
        self.c["allowed_mime_types"] = ["image/*"]
        self.assertEqual(select_attachments(message(), self.c)[0], "rejected")
        self.c["allowed_mime_types"] = []
        self.c["max_attachment_mb"] = 0.000001
        self.assertEqual(select_attachments(message(), self.c)[0], "rejected")

    def test_safe_files_and_replay(self):
        self.assertNotIn("/", safe_name("../../CON.pdf"))
        self.assertEqual(safe_name("CON.pdf"), "_CON.pdf")
        _, files, _ = select_attachments(message(), self.c)
        paths = write_files(self.c["output_dir"], "key", files * 2)
        self.assertEqual(len(set(paths)), 2)
        self.assertEqual(paths, write_files(self.c["output_dir"], "key", files * 2))
        self.assertEqual(len(list(self.c["output_dir"].rglob("*.pdf"))), 2)

    def test_nested_message_not_extracted(self):
        m = EmailMessage()
        m.set_content("outer")
        m.add_attachment(message())
        self.assertEqual(select_attachments(m, self.c)[:2], ("rejected", []))

    def test_first_start_only_new(self):
        self.c["include_existing"] = False
        fake = FakeIMAP(message().as_bytes())
        collector = self.collector(fake)
        collector.poll()
        self.assertEqual(fake.fetches, 0)
        fake.ids = [1, 2]
        collector.poll()
        self.assertEqual(fake.marked, ["2"])

    def test_store_failure_retry_does_not_duplicate(self):
        fake = FakeIMAP(message().as_bytes())
        fake.fail_store = True
        collector = self.collector(fake)
        with self.assertRaises(OSError):
            collector.poll()
        self.assertEqual(self.store.db.execute("SELECT seen FROM messages").fetchone()[0], 0)
        fake.fail_store = False
        collector.poll()
        collector.poll()
        self.assertEqual(fake.marked, ["1"])
        self.assertEqual(len(list(self.c["output_dir"].rglob("*.pdf"))), 1)
        self.assertEqual(self.store.db.execute("SELECT COUNT(*) FROM messages").fetchone()[0], 1)

    def test_reply_and_email_notice(self):
        self.c.update(reply_enabled=True, notification_email="notice@example.com",
                      allowed_extensions=[".png"])
        fake = FakeIMAP(message().as_bytes())
        collector = self.collector(fake)
        sent = []
        collector.send = lambda *args: sent.append(args)
        collector.poll()
        collector.poll()
        self.assertEqual(len(sent), 2)
        self.assertEqual({args[0] for args in sent}, {"notice@example.com", "sender@example.com"})
        self.assertIn("不符合", sent[1][2])

    def test_uncertain_send_not_repeated(self):
        self.c.update(reply_enabled=True, allowed_extensions=[".png"])
        collector = self.collector(FakeIMAP(message().as_bytes()))
        attempts = []

        def fail(*args):
            attempts.append(1)
            raise TimeoutError()

        collector.send = fail
        collector.poll()
        collector.poll()
        self.assertEqual(attempts, [1])
        self.assertEqual(self.store.db.execute("SELECT reply FROM messages").fetchone()[0], "uncertain")

    def test_generated_message_no_email_loop(self):
        m = message()
        m["X-Smail-Generated"] = "yes"
        self.assertTrue(auto_message(m))
        self.c.update(notification_email="notice@example.com", reply_enabled=True)
        collector = self.collector(FakeIMAP(m.as_bytes()))
        collector.send = lambda *a: self.fail("must not send")
        collector.poll()
        self.assertEqual(self.store.db.execute("SELECT status FROM messages").fetchone()[0], "ignored")

    def test_oversize_no_body_fetch(self):
        self.c["max_message_mb"] = 0.000001
        fake = FakeIMAP(message().as_bytes())
        self.collector(fake).poll()
        self.assertEqual(fake.fetches, 0)
        self.assertEqual(fake.marked, ["1"])

    def test_smtp_message_headers(self):
        sent = []

        class SMTP:
            def __enter__(self):
                return self

            def __exit__(self, *args):
                pass

            def login(self, *args):
                pass

            def send_message(self, mail):
                sent.append(mail)
                return {}

        collector = Collector(self.c, self.store, "secret", smtp_factory=lambda *a, **k: SMTP())
        original = message()
        original["Message-ID"] = "<original@example.com>"
        collector.send("sender@example.com", "拒收", "原因：类型错误", original)
        self.assertEqual(sent[0]["Auto-Submitted"], "auto-replied")
        self.assertEqual(sent[0]["In-Reply-To"], "<original@example.com>")
        self.assertEqual(sent[0]["X-Smail-Generated"], "yes")
        self.assertIn("类型错误", sent[0].get_content())

    def test_plain_message_and_inline_image(self):
        m = EmailMessage()
        m.set_content("no attachments")
        self.assertEqual(select_attachments(m, self.c)[0], "ignored")
        m.add_attachment(b"image", maintype="image", subtype="png",
                         filename="signature.png", disposition="inline")
        self.assertEqual(select_attachments(m, self.c)[0], "ignored")
        self.c["include_inline"] = True
        self.assertEqual(select_attachments(m, self.c)[0], "accepted")


if __name__ == "__main__":
    unittest.main()

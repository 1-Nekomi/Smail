import argparse
import json
import logging
from logging.handlers import RotatingFileHandler
import os
import time

from .config import load
from .service import Collector, Store, single_instance


def main():
    parser = argparse.ArgumentParser(description="Smail 邮件附件自动收取")
    parser.add_argument("command", choices=["check", "once", "run", "history"])
    parser.add_argument("--config", default="config.json")
    args = parser.parse_args()
    try:
        c = load(args.config)
        if args.command == "check":
            print("配置有效；授权码环境变量：" + ("已设置" if os.getenv(c["password_env"]) else "未设置"))
            print("此检查不会连接邮箱或发信。")
            return 0
        c["state_dir"].mkdir(parents=True, exist_ok=True)
        handlers = [logging.StreamHandler(),
                    RotatingFileHandler(c["state_dir"] / "smail.log", maxBytes=2_000_000,
                                        backupCount=3, encoding="utf-8")]
        logging.basicConfig(level=logging.INFO, handlers=handlers,
                            format="%(asctime)s %(levelname)s %(message)s")
        with single_instance(c["state_dir"]):
            store = Store(c["state_dir"])
            try:
                if args.command == "history":
                    for row in store.db.execute("SELECT * FROM messages ORDER BY rowid DESC LIMIT 50"):
                        print(json.dumps(dict(row), ensure_ascii=False))
                    return 0
                password = os.getenv(c["password_env"])
                if not password:
                    raise ValueError(f"请设置环境变量 {c['password_env']} 为邮箱授权码")
                service = Collector(c, store, password)
                delay = c["poll_seconds"]
                while True:
                    try:
                        service.poll()
                        delay = c["poll_seconds"]
                    except Exception as e:
                        # Provider errors can contain account details; avoid raw responses.
                        logging.error("收取失败 (%s)，请检查网络、授权码与服务器配置", type(e).__name__)
                        if args.command == "once":
                            return 1
                        delay = min(max(delay * 2, 5), 900)
                    if args.command == "once":
                        return 0
                    time.sleep(delay)
            finally:
                store.db.close()
    except KeyboardInterrupt:
        print("已停止。")
        return 0
    except (ValueError, OSError, RuntimeError) as e:
        print(f"错误：{e}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

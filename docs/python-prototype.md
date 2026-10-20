# Smail 邮件附件自动收取

Python 3.11+，无需安装第三方包。Windows 优先；其他平台可使用日志及邮件通知。
整体设计见 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 已实现

- IMAP TLS 轮询收件，QQ 默认配置，其他兼容邮箱可配置服务器。
- 图片/文档等附件按扩展名、MIME 类型和大小筛选。
- 默认保存合格附件；没有合格附件时，向原始 From 发件人回复配置文字。
- 附件成功保存并记录后设置已读；拒收和无附件邮件也设置已读。
- Windows 桌面通知、另一邮箱接收通知、控制台与轮转日志，可同时使用。
- SQLite 历史与断点续跑、网络错误退避重试、单实例锁、防文件名越界。

## 开始使用（PowerShell）

```powershell
Copy-Item config.example.json config.json
notepad config.json
```

填写 `email`、`output_dir`（可用 `D:/邮件附件`）、`notification_email`。
QQ 邮箱需要在网页版设置中开启 IMAP/SMTP 服务并生成授权码。
授权码与网页登录密码不同。配置示例使用 `imap.qq.com:993` 和 `smtp.qq.com:465`。

使用交互输入将授权码放到当前 PowerShell 会话环境变量，避免写进命令历史：

```powershell
$smailSecret = Read-Host "邮箱授权码" -AsSecureString
$env:SMAIL_PASSWORD = [System.Net.NetworkCredential]::new("", $smailSecret).Password
python -m smail check
python -m smail once
python -m smail run
```

`check` 仅检查配置，不连接邮箱。`once` 执行一次实际轮询，可能下载、标记已读和发信。
`run` 每 60 秒轮询，Ctrl+C 停止。程序必须运行，电脑也须保持联网、非休眠状态。
首次成功连接默认跳过当时所有旧邮件；建立基线后发送一封带附件的测试邮件即可验收。
配置 `include_existing: true` 可在首次连接时收取全部历史邮件（包括已读邮件）。
已有基线时更改该项不会重置进度。不要随意删除状态目录，否则可能重新处理历史邮件。

```powershell
python -m smail history
python -m unittest discover -s tests -v
```

## 配置

| 字段 | 含义 |
|---|---|
| `output_dir` / `state_dir` | 附件目录 / 状态与日志目录；相对路径以配置文件所在目录为基准 |
| `allowed_extensions` | 扩展名白名单，不区分大小写；空列表表示不限制扩展名 |
| `allowed_mime_types` | MIME 白名单，如 `["image/*", "application/pdf"]`；空列表不限制 |
| `policy` | `matching` 保存合格部分；`strict` 有不合格项则整封拒收 |
| `include_inline` | 是否收取正文内嵌图片，默认 false，避免收集签名图标 |
| `max_attachment_mb` / `max_message_mb` | 单附件 / 整封邮件大小上限 |
| `reply_enabled` | 拒收邮件是否自动回复，示例默认 true |
| `rejection_message` | 回复正文，可使用 `{reason}` 和 `{allowed_types}`；其他花括号请用 `{{`、`}}` |
| `notification` | `desktop` 桌面通知并记日志，或 `log` 仅日志 |
| `notification_email` | 邮件通知收件地址；空字符串关闭。须不同于收信邮箱 |
| `smtp_host` / `smtp_port` | SMTP 隐式 TLS 服务器；第一版不支持 STARTTLS |
| `password_env` | 授权码环境变量名；收信和发信使用同一账号授权码 |

同时开启三种通知：保持 `notification: "desktop"`，填写 `notification_email`；
日志始终保存在 `data/smail.log`。桌面气泡受 Windows 通知设置及交互桌面会话影响。
程序不提供 GUI，`history` 展示最近 50 条记录及文件完整路径。

## 行为与限制

- 客户端“拒收”是拒绝保存附件并回复，不是 SMTP 投递阶段退信，也不会删除原邮件。
- 没有附件的普通邮件只记录和标记已读，不回复；自动邮件、列表邮件、自发邮件、
  no-reply 发件人以及整封超限邮件不自动回复。内嵌图片默认不算候选附件。
- 从邮箱移除的待处理邮件会跳过；其他处理失败会停在该邮件等待下轮重试。
- 附件按稳定邮件标识建子目录，并加序号避免同名覆盖。不会自动解压或打开。
- 扩展名和 MIME 都来自邮件信息，不提供文件内容鉴真或杀毒；不支持云附件下载链接。
- 附件下载具备幂等性；SMTP 无法与本地数据库原子提交，发送超时会记录 `uncertain`，
  不自动重发。可在 `history` 核查 `reply` / `email_notice` 状态。
- SQLite 时间 `created` 为 UTC。日志采用运行系统本地时间。
- 其他邮箱仅限支持密码/授权码 IMAP 登录的服务。需要 OAuth2 的账号暂不支持。
- 尚未使用真实邮箱联调；自动测试覆盖 MIME 筛选、落盘、重试、基线和通知防循环。

## 建议的下一阶段

增加托盘设置界面与凭据库、发件人白名单、开机启动；
业务需要时再扩展多账号、按项目归档和病毒扫描。

协议与配置参考：
[Python IMAP 官方文档](https://docs.python.org/3/library/imaplib.html)、
[腾讯云 Email 连接器说明](https://main.qcloudimg.com/raw/document/product/pdf/1270_46586_cn.pdf)。

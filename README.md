# Smail

**A Windows desktop tool that automatically collects email attachments from QQ Mail and any compatible IMAP/SMTP mailbox, organizes them by rules, and can send automatic replies.**

**English** | [简体中文](README.zh-CN.md)

![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0078D6?logo=windows&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-6.5%2B-41CD52?logo=qt&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)
![Version](https://img.shields.io/badge/version-0.1.6-informational)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)

<p align="center"><img src="assets/smail.jpeg" alt="Smail application icon" width="120"></p>

## Introduction

Smail is a Windows desktop application built with **Qt 6 Widgets / C++17**. It periodically signs in to your mailbox, saves attachments that match your rules to a local folder, and optionally sends an automatic reply to the sender — so you no longer have to open the web mailbox every day and download attachments one by one.

A typical use case: receive homework, reports, images, or log files in a QQ Mail account, let Smail save the attachments automatically while it is running, and acknowledge the sender with an "attachments received" reply.

### Features

- **Automatic collection** — After you click "Start", Smail keeps polling the inbox (60 seconds by default). At startup it also processes unread mail that has not been handled yet, and it backs off and retries after network errors.
- **Rule-based filtering** — Extension allowlist (leave empty for all), optional MIME allowlist, strict mode, inline-image switch, and per-attachment / per-message size limits.
- **Three automatic replies** — Separate switches, subjects, bodies, and attachments for "with attachment", "without attachment", and "rejected"; the body editor supports inserting PNG/JPEG/GIF images.
- **No duplicate work** — Messages are tracked by UID, and progress and reply state are stored in SQLite, so a restart or reconnect never downloads or replies twice.
- **Encrypted credentials** — On Windows the authorization code is encrypted with user-scoped DPAPI and never written to the JSON config or logs.
- **Lives in the tray** — Closing the window keeps Smail running in the system tray; a notification is shown after each handled message.
- **Simple deployment** — A single executable plus the Qt and MinGW runtime libraries; no database or background service is required.

### Supported platforms

| Platform | Status |
| --- | --- |
| Windows 10 / 11 (x64) | ✅ Supported; an installer is published |
| Other desktop platforms | ⚠️ Not available at the moment |

## Installation

### Option 1: Installer (recommended)

1. Open [Releases](../../releases/latest) and download the latest installer, `Smail-<version>-windows-x64-setup.exe`.
2. (Recommended) Verify the file. Both commands below should print the same hash:

   ```powershell
   Get-FileHash .\Smail-0.1.6-windows-x64-setup.exe -Algorithm SHA256
   Get-Content .\Smail-0.1.6-windows-x64-setup.exe.sha256
   ```

3. Run the installer. It installs for the current user only, requires no administrator rights, defaults to `%LOCALAPPDATA%\Programs\Smail`, and creates desktop and Start-menu shortcuts.
4. The installer is not code-signed; Windows SmartScreen may show "Unknown publisher". After verifying the SHA-256 value, choose "More info → Run anyway" to continue.

> Quit Smail from the system tray before updating or uninstalling. The uninstaller removes only program files and shortcuts. Downloaded attachments and the configuration, history, and encrypted credentials under `%LOCALAPPDATA%\Smail\Smail` are kept. To delete the saved credential, click "Forget authorization code" in the app before uninstalling.

### Option 2: Portable ZIP (no installation)

1. Download `Smail-<version>-windows-x64-portable.zip` from [Releases](../../releases/latest).
2. (Recommended) Verify the archive with the matching `.zip.sha256` file:

   ```powershell
   Get-FileHash .\Smail-0.1.6-windows-x64-portable.zip -Algorithm SHA256
   Get-Content .\Smail-0.1.6-windows-x64-portable.zip.sha256
   ```

3. Extract the archive to a writable folder (for example `D:\Apps\Smail`), then run `Smail.exe` from the extracted `Smail` folder. The archive already contains the Qt runtime and required plugins; no separate installation is needed.
4. No installation, administrator rights, or registry entries are required. To remove the portable version, delete the extracted folder.
5. The executable is not code-signed; Windows SmartScreen may show "Unknown publisher". After verifying the SHA-256 value, choose "More info → Run anyway" to continue.
6. By default, data is stored in `%LOCALAPPDATA%\Smail\Smail` (shared with the installed version). To keep settings, history, and encrypted credentials inside the portable folder, run:

   ```powershell
   .\Smail.exe --data-dir .\data
   ```

   For convenience, create a shortcut to `Smail.exe` with that argument and set its "Start in" folder to the extracted folder.

### Option 3: Build from source

| Dependency | Requirement |
| --- | --- |
| Operating system | Windows 10 / 11 x64 |
| CMake | ≥ 3.21 |
| C++ compiler | C++17 support, matching the Qt kit (for example MinGW 13.1 or MSVC 2022) |
| Qt | ≥ 6.5 with the Core, Widgets, Network, and Sql modules |
| Ninja (optional) | Any recent version, for faster builds |

```powershell
# 1. Configure (replace the paths with your own Qt and compiler locations)
cmake -S . -B build -G Ninja `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/mingw_64" `
  -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe" `
  -DCMAKE_BUILD_TYPE=Release

# 2. Build
cmake --build build -j 4
```

> With the Visual Studio generator, remove `-G Ninja` and use `-G "Visual Studio 17 2022" -A x64`; build with `cmake --build build --config Release`.

The build produces `build/Smail.exe`. It depends on the Qt DLLs, so deploy the runtime libraries first to get a portable folder:

```powershell
# 3. Deploy the Qt runtime (windeployqt must match the Qt kit used to build)
New-Item -ItemType Directory -Force dist/Smail | Out-Null
Copy-Item build/Smail.exe dist/Smail/
& "C:/Qt/6.8.0/mingw_64/bin/windeployqt.exe" --release --no-translations dist/Smail/Smail.exe

# 4. Run
.\dist\Smail\Smail.exe
```

## Usage

1. **Enable IMAP/SMTP and create an authorization code** — For QQ Mail, sign in to the web mailbox, open "Settings → Account & Security → Security Settings", enable IMAP/SMTP, and generate an authorization code. The authorization code is not your login password.
2. **Configure the mailbox** — On the "1 邮箱设置" (Mailbox) tab, choose the mailbox type. QQ Mail comes with `imap.qq.com:993` and `smtp.qq.com:465` preconfigured; for another mailbox, enter its IMAP/SMTP servers and ports. Then enter the email address, authorization code, attachment folder, and check interval (15–3600 seconds). Select "Encrypt and remember the authorization code" and click "Save settings"; the code will be filled in automatically after a restart. Click "Forget authorization code" to delete the saved credential.
3. **Set collection rules** — On the "2 收取规则" (Rules) tab, use the "Images / Documents / Images + Documents" presets or enter extensions manually (comma-separated, case-insensitive, empty means all). You can combine them with a MIME allowlist, enable "Reject the whole message if any attachment does not match" and "Also collect inline images", and adjust the per-attachment and per-message size limits.
4. **(Optional) Configure automatic replies** — In the "自动回复" (Auto-reply) area, configure the "with attachment / without attachment / rejected" replies separately. Enable a reply, then fill in its subject (up to 200 characters) and body. Click "Insert body image…" to insert a PNG/JPEG/GIF at the cursor, and "Add files…" to attach regular files. The body supports the `{attachment_count}` (number saved), `{reason}` (processing reason), and `{allowed_types}` (extension list) placeholders. Click "Save rules" when done. You can click "Test SMTP connection" first to confirm that SMTP login works — it only connects and authenticates, without sending a message.
5. **Start collecting** — Click "Start". Smail first processes unread messages in the inbox that have not been handled yet, then polls for new mail at the configured interval. The "3 运行记录" (Activity) tab shows the processing history and live log; click "Open attachment folder" to locate saved files.
6. **Tray and exit** — Closing the window keeps Smail running in the system tray; choose "Exit" from the tray menu to stop it completely. You can click "Stop" at any time while collecting.

Tray icon: <img src="assets/smail_under.png" alt="Smail tray icon" width="32">

### Notes

- **You must click "Start" before Smail connects to the mailbox**; there is no automatic collection at startup.
- Only the inbox (INBOX) is processed. Messages are marked as read, never deleted or moved.
- Only implicit TLS (for example 993/465) with IMAP `LOGIN` and SMTP `AUTH LOGIN` is supported. OAuth2, STARTTLS, POP3, Exchange-specific protocols, and cloud-drive attachment links are not supported.
- The computer must stay powered on, online, and awake. On network errors Smail backs off and retries, at most once every 15 minutes.
- An automatic reply is sent at most once per message. Auto-submitted messages, mailing lists, messages sent by yourself, and no-reply senders are never answered.
- A single matching attachment is saved directly with its original filename; multiple attachments are stored in a per-message subfolder. Name collisions are numbered, for example `Report (2).pdf`.
- Attachment type is determined only from the file extension and MIME declaration. Smail does not verify content or scan for viruses; do not run attachments from untrusted sources.
- This version supports one account per data directory.

## Data and privacy

By default, Smail stores its runtime data in `%LOCALAPPDATA%\Smail\Smail`:

| File / directory | Contents |
| --- | --- |
| `settings.json` | Email address, servers, collection rules, and other settings (**no authorization code**) |
| `credentials/*.dpapi` | Authorization code encrypted with Windows DPAPI, isolated per account and server |
| `history.sqlite` | Message processing progress, attachment paths, and reply state |
| `smail.log` | Runtime log; rotated when it exceeds 2 MB |

- The authorization code is protected with user-scoped Windows DPAPI. The ciphertext is tied to the current user (and usually to the current computer); after switching computers or reinstalling Windows, enter it again.
- Smail does not embed an encryption key and never writes the authorization code to the JSON config or the log.
- The log and processing history contain message subjects, local paths, and other business data. Redact them before sharing.
- To use a separate data directory (for example, multiple configurations or debugging), pass `--data-dir`:

  ```powershell
  .\Smail.exe --data-dir D:\smail-data
  ```

## Project structure

```text
Smail/
├─ CMakeLists.txt
├─ include/smail/        # Public headers and data models
├─ src/
│  ├─ app/               # Entry point, main window, tray, background collector
│  ├─ core/              # Config, MIME parsing, rules, SQLite, DPAPI credentials
│  └─ net/               # IMAP / SMTP implicit-TLS clients
├─ assets/               # Application and tray icons, README images
├─ resources/            # Qt resource file and Windows icon resource
├─ LICENSE
├─ README.md
└─ README.zh-CN.md
```

This repository publishes the program sources only; tests, installer build scripts, internal design notes, and generated release artifacts are not distributed (see [`.gitignore`](.gitignore)). Build artifacts are written to the local `release/` directory and are not committed.

## FAQ

**Q: "Test SMTP connection" reports `Remote host closed` or times out.**

This is usually not a wrong authorization code. A proxy, firewall, or campus network is likely blocking the SMTP ports (465/587/25). If you use a proxy such as Clash, switch to rule/direct mode, add a `DOMAIN-SUFFIX,qq.com,DIRECT` rule, or temporarily disable the proxy or use a mobile hotspot and try again.

**Q: Smail says it is connected but no new mail arrives.**

Make sure you clicked "Start". Smail processes only the inbox, and on the first connection it skips old read messages by default (select "Also process historical mail on first connection" to include them). Check the log on the "Activity" tab and confirm that IMAP is enabled for the mailbox.

**Q: Will Smail download or reply twice?**

No. Messages that have already been handled are not downloaded again. SMTP results are recorded as `attempted` / `sent` / `uncertain`; an `uncertain` result is never retried automatically (missing a reply is preferred over sending a duplicate), so inspect it manually.

**Q: Does Smail support Gmail, Outlook, or other mailboxes?**

Try it if the provider allows IMAP `LOGIN` with implicit TLS. Accounts that require OAuth2 or STARTTLS are not supported.

**Q: Windows shows "Unknown publisher" during installation.**

The installer is not code-signed, so this warning is expected. Verify the SHA-256 value with the `.sha256` file before continuing.

## License

This project is released under the **GNU General Public License v3.0 (GPL-3.0)**. The full text is in [LICENSE](LICENSE).

Why GPL-3.0:

- **Compatible with Qt** — The open-source edition of Qt 6 is available under LGPLv3 / GPLv3, and a GPL-3.0 project can dynamically link the Qt runtime and distribute the result without an additional linking exception.
- **Stays open** — Copyleft requires modifications and derivative works to remain open source, so the project cannot be turned into a closed-source product.
- **Auditable** — Because Smail handles mailbox credentials and sends mail automatically, a clear copyleft license lets users know they can audit, build, and modify the code themselves.

Qt 6 and its runtime libraries are copyright The Qt Company Ltd. and other contributors, under their respective LGPLv3 / GPLv3 terms.

Copyright (C) 2026 1-Nekomi.

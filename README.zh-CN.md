# Smail

**支持 QQ 邮箱及任意兼容 IMAP/SMTP 的邮箱，自动收取附件、按规则归档，并可自动回复的 Windows 桌面工具。**

[English](README.md) | **简体中文**

![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0078D6?logo=windows&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-6.5%2B-41CD52?logo=qt&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)
![Version](https://img.shields.io/badge/version-0.1.6-informational)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)

<p align="center"><img src="assets/smail.jpeg" alt="Smail 应用图标" width="120"></p>

## 简介

Smail 是一个基于 **Qt 6 Widgets / C++17** 的 Windows 桌面程序。它会定时登录你的邮箱，把符合规则的附件自动保存到本地目录，并按需向发件人自动回复，免去每天打开网页邮箱、逐个下载附件的重复操作。

典型场景：用 QQ 邮箱接收作业、报表、图片或日志文件，希望程序运行期间自动把附件落盘，并给发件人回一封“已收到”的确认邮件。

### 功能特性

- **自动收取**：点击“开始收取”后持续轮询收件箱，默认每 60 秒检查一次；每次启动会补查尚未处理的未读邮件，断线后自动退避重试。
- **按规则筛选**：扩展名白名单（留空表示全部）、可选 MIME 白名单、严格模式、内嵌图片开关，以及单附件与整封邮件的大小上限。
- **三种自动回复**：有附件、无附件、附件被拒收三种场景分别配置开关、主题、正文和附件；正文支持插入 PNG/JPEG/GIF 图片。
- **不重复处理**：按 UID 跟踪邮件，使用 SQLite 持久化处理进度与回复状态；重启或断网恢复后不会重复下载、重复回复。
- **凭据加密**：Windows 下使用当前用户范围的 DPAPI 加密保存授权码，不写入配置文件与日志。
- **托盘常驻**：关闭窗口后继续在系统托盘运行，处理完成弹出通知。
- **部署简单**：程序为单个可执行文件，运行时只需随附 Qt 与 MinGW 运行库，无需安装数据库或后台服务。

### 支持平台

| 平台 | 支持情况 |
| --- | --- |
| Windows 10 / 11（x64） | ✅ 正式支持，提供安装包 |
| 其他桌面平台 | ⚠️ 暂不可用 |

## 安装

### 方式一：安装包（推荐）

1. 前往 [Releases](../../releases/latest) 下载最新安装包 `Smail-<版本>-windows-x64-setup.exe`。
2. （推荐）核对文件完整性，两个命令输出的哈希应一致：

   ```powershell
   Get-FileHash .\Smail-0.1.6-windows-x64-setup.exe -Algorithm SHA256
   Get-Content .\Smail-0.1.6-windows-x64-setup.exe.sha256
   ```

3. 双击安装包，按向导完成安装。安装仅作用于当前用户，无需管理员权限，默认安装到 `%LOCALAPPDATA%\Programs\Smail`，并创建桌面和开始菜单快捷方式。
4. 安装包未做代码签名，Windows SmartScreen 可能提示“未知发布者”；核对 SHA-256 后选择“更多信息 → 仍要运行”即可。

> 更新或卸载前，请先从系统托盘完全退出 Smail。卸载只删除程序文件和快捷方式，已下载的附件以及 `%LOCALAPPDATA%\Smail\Smail` 下的配置、历史记录和加密授权码都会保留；如需删除授权码，请在卸载前点击程序中的“忘记授权码”。

### 方式二：免安装 ZIP

1. 前往 [Releases](../../releases/latest) 下载 `Smail-<版本>-windows-x64-portable.zip`。
2. （推荐）使用同名的 `.zip.sha256` 文件核对压缩包完整性：

   ```powershell
   Get-FileHash .\Smail-0.1.6-windows-x64-portable.zip -Algorithm SHA256
   Get-Content .\Smail-0.1.6-windows-x64-portable.zip.sha256
   ```

3. 将压缩包解压到可写目录（例如 `D:\Apps\Smail`），然后运行解压出的 `Smail` 文件夹中的 `Smail.exe`。压缩包已包含 Qt 运行库和所需插件，无需额外安装。
4. 无需安装、管理员权限或注册表写入；删除解压出的文件夹即可移除免安装版。
5. 程序未做代码签名，SmartScreen 可能提示“未知发布者”；核对 SHA-256 后选择“更多信息 → 仍要运行”即可。
6. 默认数据仍保存在 `%LOCALAPPDATA%\Smail\Smail`（与安装版共用）。如果希望配置、历史和加密授权码跟随程序目录，请运行：

   ```powershell
   .\Smail.exe --data-dir .\data
   ```

   也可以创建带该参数的快捷方式，并把“起始位置”设为解压目录。

### 方式三：从源码编译

| 依赖 | 要求 |
| --- | --- |
| 操作系统 | Windows 10 / 11 x64 |
| CMake | ≥ 3.21 |
| C++ 编译器 | 支持 C++17，且与 Qt 套件匹配（如 MinGW 13.1 或 MSVC 2022） |
| Qt | ≥ 6.5，需要 Core、Widgets、Network、Sql 模块 |
| Ninja（可选） | 任意较新版本，用于更快的构建 |

```powershell
# 1. 配置工程（请替换为本机的 Qt 与编译器路径）
cmake -S . -B build -G Ninja `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/mingw_64" `
  -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe" `
  -DCMAKE_BUILD_TYPE=Release

# 2. 编译
cmake --build build -j 4
```

> 使用 Visual Studio 生成器时，配置阶段去掉 `-G Ninja` 并改用 `-G "Visual Studio 17 2022" -A x64`，编译命令改为 `cmake --build build --config Release`。

编译产物为 `build/Smail.exe`。它依赖 Qt 动态库，直接双击通常无法启动，需要先部署运行库，得到一个可整体拷走的免安装目录：

```powershell
# 3. 部署 Qt 运行库（windeployqt 需与编译所用 Qt 套件一致）
New-Item -ItemType Directory -Force dist/Smail | Out-Null
Copy-Item build/Smail.exe dist/Smail/
& "C:/Qt/6.8.0/mingw_64/bin/windeployqt.exe" --release --no-translations dist/Smail/Smail.exe

# 4. 运行
.\dist\Smail\Smail.exe
```

## 使用

1. **开启 IMAP/SMTP 并获取授权码**：以 QQ 邮箱为例，登录网页版邮箱，进入“设置 → 账号与安全 → 安全设置”，开启 IMAP/SMTP 服务并按提示生成授权码。授权码不是网页登录密码。
2. **填写邮箱设置**：在“1 邮箱设置”中选择邮箱类型。QQ 邮箱已内置 `imap.qq.com:993` 与 `smtp.qq.com:465`；自定义邮箱需填写自己的 IMAP/SMTP 服务器和端口。随后输入邮箱地址、授权码、附件保存目录和检查间隔（15–3600 秒）。勾选“加密记住授权码”并点击“保存设置”后，重启程序会自动填入；点击“忘记授权码”可删除已保存的凭据。
3. **设置收取规则**：切换到“2 收取规则”，用“图片 / 文档 / 图片 + 文档”快捷填充扩展名，或手动输入（逗号分隔，不区分大小写，留空表示全部）。可叠加 MIME 白名单，按需开启“任一附件不符就整封拒收”和“同时收取正文内嵌图片”，并调整单附件与整封邮件的大小上限。
4. **（可选）配置自动回复**：在“自动回复”区域分别配置“有附件 / 无附件 / 拒收”三种回复。勾选启用后填写回复主题（最多 200 个字符）和正文；点击“插入正文图片…”可在光标处插入 PNG/JPEG/GIF，点击“添加文件…”可附加普通文件。正文支持 `{attachment_count}`（已保存数量）、`{reason}`（处理原因）、`{allowed_types}`（扩展名列表）三个占位符。完成后点击“保存收取规则”。可先点击“测试 SMTP 发信连接”确认网络能够登录 SMTP——该操作只连接和登录，不会发送邮件。
5. **开始收取**：点击“开始收取”。程序会先处理收件箱中尚未处理的未读邮件，然后按检查间隔轮询新邮件。在“3 运行记录”中可以查看处理历史和实时日志，点击“打开附件目录”可直接定位已保存的文件。
6. **托盘与退出**：关闭窗口时程序会最小化到系统托盘继续运行；右键托盘图标选择“退出”才会彻底结束。收取过程中可随时点击“停止”。

托盘图标：<img src="assets/smail_under.png" alt="Smail 托盘图标" width="32">

### 注意事项

- **必须手动点击“开始收取”才会连接邮箱**，程序没有开机自动收取功能。
- 只处理收件箱（INBOX），只把邮件标记为已读，不删除、不移动邮件。
- 仅支持隐式 TLS（如 993/465）以及 IMAP `LOGIN` / SMTP `AUTH LOGIN`；不支持 OAuth2、STARTTLS、POP3、Exchange 专用协议和网盘/云附件链接。
- 电脑需要保持开机、联网且不休眠。网络异常时程序会自动退避重试，最长 15 分钟一次。
- 自动回复对每封邮件最多发送一次；自动邮件、邮件列表、自己发出的邮件和 no-reply 发件人不会被回复。
- 单个匹配附件会直接以原文件名保存；多个附件会保存在以邮件命名的子目录中。同名文件自动编号，如 `报告 (2).pdf`。
- 附件类型只按扩展名和 MIME 声明判断，不做内容鉴真或病毒扫描，请勿直接运行来源不明的附件。
- 当前版本为单账号：每个数据目录只保存一套邮箱设置。

## 数据与隐私

程序默认把运行数据写在 `%LOCALAPPDATA%\Smail\Smail`：

| 文件 / 目录 | 内容 |
| --- | --- |
| `settings.json` | 邮箱地址、服务器、收取规则等配置（**不包含授权码**） |
| `credentials/*.dpapi` | 经 Windows DPAPI 加密的授权码，按邮箱账号与服务器隔离 |
| `history.sqlite` | 邮件处理进度、附件路径与回复状态 |
| `smail.log` | 运行日志，超过 2 MB 自动轮转 |

- 授权码使用当前 Windows 用户范围的 DPAPI 加密，密文绑定当前用户（通常也绑定当前电脑）；更换电脑或重装系统后需要重新输入。
- 程序不内置加密密钥，也不会把授权码写入 JSON 配置或日志。
- 日志和处理历史包含邮件主题、本地路径等业务信息，对外分享前请自行脱敏。
- 如需独立的数据目录（例如多份配置或调试），可传入 `--data-dir`：

  ```powershell
  .\Smail.exe --data-dir D:\smail-data
  ```

## 项目结构

```text
Smail/
├─ CMakeLists.txt
├─ include/smail/        # 公共头文件与数据模型
├─ src/
│  ├─ app/               # 程序入口、主窗口、托盘、后台收取线程
│  ├─ core/              # 配置、MIME 解析、收取规则、SQLite、DPAPI 凭据
│  └─ net/               # IMAP / SMTP 隐式 TLS 客户端
├─ assets/               # 应用图标、托盘图标与 README 图片
├─ resources/            # Qt 资源文件与 Windows 图标资源
├─ LICENSE
├─ README.md
└─ README.zh-CN.md
```

本仓库只发布程序源码；测试、安装包构建脚本、内部设计文档和生成的发布产物不参与分发（见 [`.gitignore`](.gitignore)）。构建产物写入本地 `release/` 目录，不提交到版本库。

## 常见问题

**Q：点击“测试 SMTP 发信连接”时报 `Remote host closed` 或连接超时？**

通常不是授权码错误，而是代理、防火墙或校园网封禁了 SMTP 端口（465/587/25）。如果使用 Clash 等代理，请切换到“规则 / 直连”模式，添加 `DOMAIN-SUFFIX,qq.com,DIRECT` 规则，或暂时退出代理、改用手机热点后再测试。

**Q：显示已连接，但收不到新邮件？**

确认已经点击“开始收取”；程序只处理收件箱，且在首次连接时默认跳过旧的已读邮件（可勾选“首次连接时也处理历史邮件”）。同时检查“运行记录”中的日志，并确认邮箱已开启 IMAP 服务。

**Q：会不会重复下载或重复回复？**

不会重复下载已处理过的 UID。SMTP 发送结果会记录为 `attempted` / `sent` / `uncertain`，其中 `uncertain` 不会自动重发（宁可漏发也不重复），需要人工核查。

**Q：支持 Gmail、Outlook 等其他邮箱吗？**

只要服务商允许 IMAP `LOGIN` 和隐式 TLS 就可以尝试。强制要求 OAuth2 或 STARTTLS 的账号暂不支持。

**Q：安装时提示“未知发布者”？**

安装包未做代码签名，因此会出现该提示。请使用随包的 SHA-256 文件核对安装包完整性后再继续安装。

## 开源协议

本项目采用 **GNU General Public License v3.0（GPL-3.0）** 发布，完整条款见 [LICENSE](LICENSE)。

选择 GPL-3.0 的原因：

- **与 Qt 的许可兼容**：Qt 6 开源版以 LGPLv3 / GPLv3 提供，GPL-3.0 项目动态链接 Qt 运行库即可合规分发，无需额外的链接例外；
- **保持开源**：Copyleft 要求基于本项目的修改和衍生作品继续以开源方式发布，避免被直接闭源分发；
- **便于审计**：本项目涉及邮箱授权码和自动发信，明确的 copyleft 让使用者清楚代码可以自行审计、构建和修改。

Qt 6 及其运行库的版权归 The Qt Company Ltd. 及其他贡献者所有，遵循其各自的 LGPLv3 / GPLv3 条款。

Copyright (C) 2026 1-Nekomi.

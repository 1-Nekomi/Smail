Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!ifndef APP_ID
!define APP_ID "Smail"
!endif
!ifndef APP_NAME
!define APP_NAME "Smail"
!endif
!define REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}"
Name "${APP_NAME}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP_ID}"
InstallDirRegKey HKCU "${REGKEY}" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey /LANG=2052 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=2052 "FileDescription" "Smail 邮件附件收取安装程序"
VIAddVersionKey /LANG=2052 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=2052 "LegalCopyright" "Smail contributors"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "将安装 Smail 邮件附件收取程序及所需运行库。$\r$\n$\r$\n仅安装到当前用户，不需要管理员权限。$\r$\n$\r$\n更新前请右键托盘中的 Smail 图标并选择退出。"
!define MUI_FINISHPAGE_RUN "$INSTDIR\Smail.exe"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!define MUI_UNCONFIRMPAGE_TEXT_TOP "将删除程序和快捷方式。下载的附件、配置、处理历史和加密授权码会保留。"
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "需要 64 位 Windows。" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    ${IfNot} ${AtLeastWin10}
        MessageBox MB_OK|MB_ICONSTOP "需要 Windows 10 或更新版本。" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    SetShellVarContext current
    FindWindow $0 "" "Smail · 邮件附件收取"
    ${If} $0 != 0
        MessageBox MB_OK|MB_ICONSTOP "请先退出正在运行的 Smail，再安装。" /SD IDOK
        SetErrorLevel 2
        Abort
    ${EndIf}
FunctionEnd
Section "Smail"
    !include "${INSTALL_MANIFEST}"
    SetOutPath "$INSTDIR"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\Smail.exe"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\卸载.lnk" "$INSTDIR\Uninstall.exe"
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\Smail.exe"
    WriteRegStr HKCU "${REGKEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKCU "${REGKEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKCU "${REGKEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${REGKEY}" "DisplayIcon" "$INSTDIR\Smail.exe"
    WriteRegStr HKCU "${REGKEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
    WriteRegStr HKCU "${REGKEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
    WriteRegDWORD HKCU "${REGKEY}" "NoModify" 1
    WriteRegDWORD HKCU "${REGKEY}" "NoRepair" 1
SectionEnd
Function un.onInit
    SetShellVarContext current
    FindWindow $0 "" "Smail · 邮件附件收取"
    ${If} $0 != 0
        MessageBox MB_OK|MB_ICONSTOP "请先退出正在运行的 Smail，再卸载。" /SD IDOK
        SetErrorLevel 2
        Abort
    ${EndIf}
FunctionEnd
Section "Uninstall"
    ; Delete only the package inventory, never recursively remove user data.
    !include "${UNINSTALL_MANIFEST}"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    Delete "$DESKTOP\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\卸载.lnk"
    RMDir "$SMPROGRAMS\${APP_NAME}"
    DeleteRegKey HKCU "${REGKEY}"
SectionEnd

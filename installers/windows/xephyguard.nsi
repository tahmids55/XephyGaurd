; XephyGuard agent installer for Windows (NSIS 3; builds on Linux with `makensis`).
;
; Interactive:  XephyGuard-Setup.exe
; Silent:       XephyGuard-Setup.exe /S /KEY=<enrollment key> [/CCC=<address>] [/CAFP=<sha256>]
;               (GPO startup script, Intune Win32 app, PDQ Deploy, or the imaging task sequence)
;
; Installs: LocalSystem service "XephyGuardAgent" (auto start, restart on failure), the session
; helper (launched by the service into each user session; no Run key to delete), the lab CA in
; LocalMachine\Root, %ProgramData%\XephyGuard (admin/SYSTEM-only ACL), an outbound firewall rule and
; managed Edge/Chrome/Firefox policies. Uninstall requires administrator rights; stopping the
; service during a session raises an alert on the CCC.

Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

!ifndef VERSION
  !define VERSION "0.1.0"
!endif
!ifndef BUILD_DIR
  !define BUILD_DIR "..\..\agent\build-windows\Release"
!endif

Name "XephyGuard Agent ${VERSION}"
OutFile "XephyGuard-Setup-${VERSION}.exe"
InstallDir "$PROGRAMFILES64\XephyGuard"
RequestExecutionLevel admin
ShowInstDetails show
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "XephyGuard Agent"
VIAddVersionKey "FileVersion" "${VERSION}"

Var KEY
Var CCC
Var CAFP

!insertmacro MUI_PAGE_LICENSE "notice.txt"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "XephyGuard requires 64-bit Windows." /SD IDOK
    Abort
  ${EndIf}
  SetRegView 64
  ${GetParameters} $0
  ${GetOptions} $0 "/KEY=" $KEY
  ${GetOptions} $0 "/CCC=" $CCC
  ${GetOptions} $0 "/CAFP=" $CAFP
FunctionEnd

Section "Agent" SecAgent
  SetOutPath "$INSTDIR"
  ; stop a previous version
  nsExec::Exec 'sc.exe stop XephyGuardAgent'
  Sleep 2000
  File "${BUILD_DIR}\xephyguard-agent.exe"
  File "${BUILD_DIR}\xephyguard-helper.exe"
  File /nonfatal "${BUILD_DIR}\*.dll"
  File "notice.txt"

  ; configuration directory: SYSTEM + Administrators only
  CreateDirectory "$COMMONPROGRAMDATA\XephyGuard"
  CreateDirectory "$COMMONPROGRAMDATA\XephyGuard\data"
  CreateDirectory "$COMMONPROGRAMDATA\XephyGuard\logs"
  SetOutPath "$COMMONPROGRAMDATA\XephyGuard"
  File "signatures.json"
  File /nonfatal "ca.crt"
  nsExec::ExecToLog 'icacls "$COMMONPROGRAMDATA\XephyGuard" /inheritance:r /grant:r "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F"'
  nsExec::ExecToLog 'icacls "$INSTDIR" /inheritance:r /grant:r "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-32-545:(OI)(CI)RX"'

  ; tamper manifest (expected hashes of our binaries)
  nsExec::ExecToStack 'powershell -NoProfile -Command "Get-FileHash -Algorithm SHA256 \"$INSTDIR\xephyguard-agent.exe\",\"$INSTDIR\xephyguard-helper.exe\" | ForEach-Object { $$_.Hash.ToLower() + \"  \" + $$_.Path } | Set-Content -Encoding ascii \"$COMMONPROGRAMDATA\XephyGuard\manifest.sha256\""'

  ; lab CA -> LocalMachine\Root
  ${If} ${FileExists} "$COMMONPROGRAMDATA\XephyGuard\ca.crt"
    nsExec::ExecToLog 'certutil -addstore -f Root "$COMMONPROGRAMDATA\XephyGuard\ca.crt"'
  ${EndIf}

  ; service: LocalSystem, automatic, restart on failure (5 s, 5 s, 30 s), reset after 1 day
  nsExec::ExecToLog 'sc.exe create XephyGuardAgent binPath= "\"$INSTDIR\xephyguard-agent.exe\" run" start= auto obj= LocalSystem DisplayName= "XephyGuard Agent"'
  nsExec::ExecToLog 'sc.exe description XephyGuardAgent "Lab AI-use monitoring (see the XephyGuard notice)."'
  nsExec::ExecToLog 'sc.exe failure XephyGuardAgent reset= 86400 actions= restart/5000/restart/5000/restart/30000'
  nsExec::ExecToLog 'sc.exe failureflag XephyGuardAgent 1'
  ; only administrators may stop/reconfigure the service (default DACL minus interactive users' control)
  nsExec::ExecToLog 'sc.exe sdset XephyGuardAgent "D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)(A;;CCLCSWLOCRRC;;;IU)(A;;CCLCSWLOCRRC;;;SU)"'

  ; outbound firewall rule for the agent only (no inbound ports are opened)
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="XephyGuard Agent"'
  nsExec::ExecToLog 'netsh advfirewall firewall add rule name="XephyGuard Agent" dir=out action=allow program="$INSTDIR\xephyguard-agent.exe" enable=yes'

  ; managed browser policies (see installers/browser-policies/README.md)
  WriteRegDWORD HKLM "Software\Policies\Microsoft\Edge" "HubsSidebarEnabled" 0
  WriteRegDWORD HKLM "Software\Policies\Microsoft\Edge" "CopilotPageContext" 0
  WriteRegDWORD HKLM "Software\Policies\Microsoft\Edge" "CopilotCDPPageContext" 0
  WriteRegDWORD HKLM "Software\Policies\Microsoft\Edge" "EdgeEntraCopilotPageContext" 0
  WriteRegDWORD HKLM "Software\Policies\Microsoft\Edge" "ComposeInlineEnabled" 0
  WriteRegDWORD HKLM "Software\Policies\Microsoft\Edge" "Microsoft365CopilotChatIconEnabled" 0
  WriteRegDWORD HKLM "Software\Policies\Microsoft\Edge" "QuicAllowed" 0
  WriteRegStr   HKLM "Software\Policies\Microsoft\Edge" "DnsOverHttpsMode" "off"
  WriteRegDWORD HKLM "Software\Policies\Google\Chrome" "GenAiDefaultSettings" 2
  WriteRegDWORD HKLM "Software\Policies\Google\Chrome" "GeminiSettings" 1
  WriteRegDWORD HKLM "Software\Policies\Google\Chrome" "HelpMeWriteSettings" 2
  WriteRegDWORD HKLM "Software\Policies\Google\Chrome" "DevToolsGenAiSettings" 2
  WriteRegDWORD HKLM "Software\Policies\Google\Chrome" "GenAILocalFoundationalModelSettings" 1
  WriteRegDWORD HKLM "Software\Policies\Google\Chrome" "QuicAllowed" 0
  WriteRegStr   HKLM "Software\Policies\Google\Chrome" "DnsOverHttpsMode" "off"
  WriteRegDWORD HKLM "Software\Policies\Mozilla\Firefox\DNSOverHTTPS" "Enabled" 0
  WriteRegDWORD HKLM "Software\Policies\Mozilla\Firefox\DNSOverHTTPS" "Locked" 1
  WriteRegDWORD HKLM "Software\Policies\Mozilla\Firefox\Certificates" "ImportEnterpriseRoots" 1
  ; Windows Copilot
  WriteRegDWORD HKLM "Software\Policies\Microsoft\Windows\WindowsCopilot" "TurnOffWindowsCopilot" 1

  ; login banner (transparency)
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Policies\System" "legalnoticecaption" "This computer is monitored by XephyGuard"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Policies\System" "legalnoticetext" "During lab sessions AI-tool use is recorded and, in tests, blocked. Keystrokes, clipboard text and screen content are never recorded."

  ; enrollment (silent installs pass /KEY)
  ${If} $KEY != ""
    StrCpy $1 ""
    ${If} $CCC != ""
      StrCpy $1 "--ccc $CCC"
    ${EndIf}
    ${If} $CAFP != ""
      StrCpy $1 "$1 --ca-fingerprint $CAFP"
    ${EndIf}
    nsExec::ExecToLog '"$INSTDIR\xephyguard-agent.exe" enroll --key $KEY $1 --no-wait'
  ${EndIf}
  nsExec::ExecToLog 'sc.exe start XephyGuardAgent'

  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\XephyGuard" "DisplayName" "XephyGuard Agent"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\XephyGuard" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\XephyGuard" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\XephyGuard" "NoModify" 1
SectionEnd

Section "Uninstall"
  SetRegView 64
  ; The agent announces the stop to the CCC (clean shutdown event -> alert if a session is active).
  nsExec::ExecToLog 'sc.exe stop XephyGuardAgent'
  Sleep 3000
  nsExec::ExecToLog 'sc.exe delete XephyGuardAgent'
  nsExec::ExecToLog 'taskkill /F /IM xephyguard-helper.exe'
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="XephyGuard Agent"'
  Delete "$INSTDIR\*.*"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\XephyGuard"
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Policies\System" "legalnoticecaption"
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Policies\System" "legalnoticetext"
  ; configuration and the lab CA are kept on purpose; remove with: certutil -delstore Root "XephyGuard Lab CA"
SectionEnd

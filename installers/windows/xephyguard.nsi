; XephyGuard agent installer for Windows (NSIS 3; builds on Linux with `makensis`).
;
; Interactive:  XephyGuard-Setup-${VERSION}.exe
; Silent:       XephyGuard-Setup-${VERSION}.exe /S /ENROLLKEY=<key> /CAFP=<sha256> [/CCC=<address>]
;               (GPO startup script, Intune Win32 app, PDQ Deploy, or imaging task sequence;
;                /KEY=<key> is also accepted as a backward-compatible alias for /ENROLLKEY=)
;
; Security note (R10): Never embed enrollment keys, CA fingerprints, or lab-specific secrets
; in this script or in the compiled installer binary. Always supply them at deploy time.
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
SetOverwrite on
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "XephyGuard Agent"
VIAddVersionKey "FileVersion" "${VERSION}"

Var KEY
Var CCC
Var CAFP

; Dialog control handles
Var Dialog
Var Hwnd_CCC
Var Hwnd_KEY
Var Hwnd_CAFP

!insertmacro MUI_PAGE_LICENSE "notice.txt"
Page custom ServerConfigPage ServerConfigPageLeave
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
  StrCpy $KEY ""
  StrCpy $CCC ""
  StrCpy $CAFP ""
  ${GetParameters} $0
  ${GetOptions} $0 "/ENROLLKEY=" $KEY
  ${If} $KEY == ""
    ${GetOptions} $0 "/KEY=" $KEY
  ${EndIf}
  ${GetOptions} $0 "/CCC=" $CCC
  ${GetOptions} $0 "/CAFP=" $CAFP
FunctionEnd

Function ServerConfigPage
  ; If running silently or enrollment key was already supplied via command line, skip custom UI page
  ${If} ${Silent}
  ${OrIf} $KEY != ""
    Abort
  ${EndIf}

  nsDialogs::Create 1018
  Pop $Dialog
  ${If} $Dialog == error
    Abort
  ${EndIf}

  !insertmacro MUI_HEADER_TEXT "XephyGuard Server Connection" "Enter the central server (CCC) connection details from the 'Add a PC' dashboard page."

  ; Label and Edit for CCC Server Address
  ${NSD_CreateLabel} 0 0 100% 12u "CCC Server IP Address or Hostname (leave blank for UDP auto-discovery):"
  Pop $0
  ${NSD_CreateText} 0 14u 100% 12u "$CCC"
  Pop $Hwnd_CCC

  ; Label and Edit for Enrollment Key (never pre-filled in build)
  ${NSD_CreateLabel} 0 34u 100% 12u "Lab Enrollment Key (required):"
  Pop $0
  ${NSD_CreateText} 0 48u 100% 12u "$KEY"
  Pop $Hwnd_KEY

  ; Label and Edit for CA Fingerprint (never pre-filled in build)
  ${NSD_CreateLabel} 0 68u 100% 12u "Server CA SHA-256 Fingerprint (from CCC 'Add a PC' page):"
  Pop $0
  ${NSD_CreateText} 0 82u 100% 12u "$CAFP"
  Pop $Hwnd_CAFP

  nsDialogs::Show
FunctionEnd

Function ServerConfigPageLeave
  ${NSD_GetText} $Hwnd_CCC $CCC
  ${NSD_GetText} $Hwnd_KEY $KEY
  ${NSD_GetText} $Hwnd_CAFP $CAFP

  ${If} $KEY == ""
    MessageBox MB_ICONEXCLAMATION "Lab Enrollment Key is required (see the CCC 'Add a PC' page)."
    Abort
  ${EndIf}
  ${If} $CAFP == ""
  ${AndIfNot} ${FileExists} "$EXEDIR\ca.crt"
    MessageBox MB_ICONEXCLAMATION "Server CA SHA-256 Fingerprint is required unless ca.crt is placed next to the installer."
    Abort
  ${EndIf}
FunctionEnd

Section "Agent" SecAgent
  DetailPrint "Stopping existing XephyGuard service and processes..."
  ; Disable auto-restart before killing processes so Windows SCM does not respawn them mid-install
  nsExec::ExecToLog 'sc.exe failure XephyGuardAgent reset= 0 actions= ""'
  nsExec::ExecToLog 'sc.exe stop XephyGuardAgent'
  nsExec::ExecToLog 'taskkill /F /T /IM xephyguard-agent.exe'
  nsExec::ExecToLog 'taskkill /F /T /IM xephyguard-helper.exe'
  Sleep 2000
  nsExec::ExecToLog 'taskkill /F /T /IM xephyguard-agent.exe'
  nsExec::ExecToLog 'taskkill /F /T /IM xephyguard-helper.exe'

  CreateDirectory "$INSTDIR"
  CreateDirectory "$COMMONPROGRAMDATA\XephyGuard"
  CreateDirectory "$COMMONPROGRAMDATA\XephyGuard\data"
  CreateDirectory "$COMMONPROGRAMDATA\XephyGuard\logs"

  ; Ensure Administrators and SYSTEM have full write permissions before extracting files
  nsExec::ExecToLog 'icacls "$INSTDIR" /grant "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F"'
  nsExec::ExecToLog 'icacls "$COMMONPROGRAMDATA\XephyGuard" /grant "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F"'
  nsExec::ExecToLog 'attrib -R -S -H "$INSTDIR\*.*" /S'
  nsExec::ExecToLog 'attrib -R -S -H "$COMMONPROGRAMDATA\XephyGuard\*.*" /S'

  ; Move any locked executables out of the way so File extraction never fails with sharing violation
  Delete /REBOOTOK "$INSTDIR\xephyguard-agent.exe.old"
  Delete /REBOOTOK "$INSTDIR\xephyguard-helper.exe.old"
  ${If} ${FileExists} "$INSTDIR\xephyguard-agent.exe"
    Rename "$INSTDIR\xephyguard-agent.exe" "$INSTDIR\xephyguard-agent.exe.old"
    Delete /REBOOTOK "$INSTDIR\xephyguard-agent.exe.old"
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\xephyguard-helper.exe"
    Rename "$INSTDIR\xephyguard-helper.exe" "$INSTDIR\xephyguard-helper.exe.old"
    Delete /REBOOTOK "$INSTDIR\xephyguard-helper.exe.old"
  ${EndIf}

  SetOutPath "$INSTDIR"
  File "${BUILD_DIR}\xephyguard-agent.exe"
  File "${BUILD_DIR}\xephyguard-helper.exe"
  File /nonfatal "${BUILD_DIR}\*.dll"
  File "notice.txt"

  ; configuration directory: SYSTEM + Administrators only
  SetOutPath "$COMMONPROGRAMDATA\XephyGuard"
  File "signatures.json"
  File /nonfatal "ca.crt"
  ${If} ${FileExists} "$EXEDIR\ca.crt"
    CopyFiles /SILENT "$EXEDIR\ca.crt" "$COMMONPROGRAMDATA\XephyGuard\ca.crt"
  ${EndIf}
  nsExec::ExecToLog 'icacls "$COMMONPROGRAMDATA\XephyGuard" /inheritance:r /grant:r "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F"'
  nsExec::ExecToLog 'icacls "$INSTDIR" /inheritance:r /grant:r "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-32-545:(OI)(CI)RX"'

  ; tamper manifest (expected hashes of our binaries)
  nsExec::ExecToStack 'powershell -NoProfile -Command "Get-FileHash -Algorithm SHA256 \"$INSTDIR\xephyguard-agent.exe\",\"$INSTDIR\xephyguard-helper.exe\" | ForEach-Object { $$_.Hash.ToLower() + \"  \" + $$_.Path } | Set-Content -Encoding ascii \"$COMMONPROGRAMDATA\XephyGuard\manifest.sha256\""'

  ; lab CA -> LocalMachine\Root
  ${If} ${FileExists} "$COMMONPROGRAMDATA\XephyGuard\ca.crt"
    nsExec::ExecToLog 'certutil -addstore -f Root "$COMMONPROGRAMDATA\XephyGuard\ca.crt"'
  ${EndIf}

  ; service: LocalSystem, automatic, restart on failure (5 s, 5 s, 30 s), reset after 1 day
  nsExec::ExecToLog 'sc.exe delete XephyGuardAgent'
  Sleep 500
  nsExec::ExecToLog 'sc.exe create XephyGuardAgent binPath= "\"$INSTDIR\xephyguard-agent.exe\" run" start= auto obj= LocalSystem DisplayName= "XephyGuard Agent"'
  nsExec::ExecToLog 'sc.exe description XephyGuardAgent "Lab AI-use monitoring (see the XephyGuard notice)."'
  nsExec::ExecToLog 'sc.exe failure XephyGuardAgent reset= 86400 actions= restart/5000/restart/5000/restart/30000'
  nsExec::ExecToLog 'sc.exe failureflag XephyGuardAgent 1'
  ; only administrators may stop/reconfigure the service
  nsExec::ExecToLog 'sc.exe sdset XephyGuardAgent "D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)(A;;CCLCSWLOCRRC;;;IU)(A;;CCLCSWLOCRRC;;;SU)"'

  ; outbound firewall rule for the agent only (no inbound ports are opened)
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="XephyGuard Agent"'
  nsExec::ExecToLog 'netsh advfirewall firewall add rule name="XephyGuard Agent" dir=out action=allow program="$INSTDIR\xephyguard-agent.exe" enable=yes'

  ; managed browser policies
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
  WriteRegDWORD HKLM "Software\Policies\Microsoft\Windows\WindowsCopilot" "TurnOffWindowsCopilot" 1

  ; login banner
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Policies\System" "legalnoticecaption" "This computer is monitored by XephyGuard"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Policies\System" "legalnoticetext" "During lab sessions AI-tool use is recorded and, in tests, blocked. Keystrokes, clipboard text and screen content are never recorded."

  ; write deploy-time CCC parameters into agent.toml before enrollment
  ${IfNot} ${FileExists} "$COMMONPROGRAMDATA\XephyGuard\agent.toml"
    FileOpen $2 "$COMMONPROGRAMDATA\XephyGuard\agent.toml" w
    FileWrite $2 "# XephyGuard agent configuration. Managed by the agent; edit only with administrator rights.$\r$\n"
    FileWrite $2 "[ccc]$\r$\n"
    FileWrite $2 'host = "$CCC"$\r$\n'
    FileWrite $2 "api_port = 8443$\r$\n"
    FileWrite $2 "discovery_port = 47474$\r$\n"
    FileWrite $2 'ca_fingerprint = "$CAFP"$\r$\n'
    FileClose $2
  ${EndIf}

  ; enrollment (parameters passed at deploy time via /ENROLLKEY=, /CAFP=, /CCC= or interactive dialog)
  ${If} $KEY != ""
    StrCpy $1 ""
    ${If} $CCC != ""
      StrCpy $1 "$1 --ccc $\"$CCC$\""
    ${EndIf}
    ${If} $CAFP != ""
      StrCpy $1 "$1 --ca-fingerprint $\"$CAFP$\""
    ${EndIf}
    DetailPrint "Enrolling with CCC Server ($CCC)..."
    nsExec::ExecToLog '"$INSTDIR\xephyguard-agent.exe" enroll --key "$KEY" $1 --no-wait'
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
  nsExec::ExecToLog 'sc.exe failure XephyGuardAgent reset= 0 actions= ""'
  nsExec::ExecToLog 'sc.exe stop XephyGuardAgent'
  nsExec::ExecToLog 'taskkill /F /T /IM xephyguard-agent.exe'
  nsExec::ExecToLog 'taskkill /F /T /IM xephyguard-helper.exe'
  Sleep 2000
  nsExec::ExecToLog 'sc.exe delete XephyGuardAgent'
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="XephyGuard Agent"'
  Delete /REBOOTOK "$INSTDIR\*.*"
  RMDir /REBOOTOK "$INSTDIR"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\XephyGuard"
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Policies\System" "legalnoticecaption"
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Policies\System" "legalnoticetext"
SectionEnd

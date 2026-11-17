/*
Sable, a native port of NightPDF
Copyright (C) 2021  Advaith Madhukar
Copyright (C) 2026  Abhilash Kar

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

; Sable's installer. NightPDF shipped an NSIS installer too (electron-builder);
; this one is written by hand for the native app. scripts/installer.py passes
; VERSION, VERSION_NUMBER, ARCH, SOURCE, UNINSTALL_LIST and OUTFILE.

!macro Required name
	!ifndef ${name}
		!error "${name} is missing: build this through scripts/installer.py"
	!endif
!macroend
!insertmacro Required VERSION
!insertmacro Required VERSION_NUMBER
!insertmacro Required ARCH
!insertmacro Required SOURCE
!insertmacro Required UNINSTALL_LIST
!insertmacro Required OUTFILE

Unicode true
ManifestDPIAware true
ManifestSupportedOS all
; per user, like most apps people download, so there is no UAC prompt
RequestExecutionLevel user
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!define APP "Sable"
!define EXE "Sable.exe"
!define PUBLISHER "Abhilash Kar"
!define URL "https://github.com/abhilxsh07/sable"
!define UNINSTALLER "Uninstall Sable.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Sable"
!define PROGID "Sable.PDF"
!define CAPABILITIES "Software\Sable\Capabilities"
; Sable bootstraps Windows App SDK 2.5 (DECISIONS.md, pinned versions)
!define RUNTIME_MIN "2.5.1.0"
; see: https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/downloads
!define RUNTIME_PAGE "https://learn.microsoft.com/windows/apps/windows-app-sdk/downloads"
!if ${ARCH} == "ARM64"
	!define RUNTIME_ARCH "Arm64"
	!define RUNTIME_URL "https://aka.ms/windowsappsdk/2.5/2.5.1/windowsappruntimeinstall-arm64.exe"
!else
	!define RUNTIME_ARCH "X64"
	!define RUNTIME_URL "https://aka.ms/windowsappsdk/2.5/2.5.1/windowsappruntimeinstall-x64.exe"
!endif

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
BrandingText "${APP} ${VERSION}"

VIProductVersion "${VERSION_NUMBER}"
VIFileVersion "${VERSION_NUMBER}"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APP} Setup"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "GPL-2.0-only"

Var RuntimeMissing

!define MUI_ICON "..\app\assets\icon.ico"
!define MUI_UNICON "..\app\assets\icon.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "welcome.bmp"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TITLE "Welcome to ${APP}"
!define MUI_WELCOMEPAGE_TEXT "${APP} is a dark-mode PDF reader for Windows, based on NightPDF.$\r$\n$\r$\nThis will install ${APP} ${VERSION} on your computer. You can choose where it goes and whether it opens your PDFs.$\r$\n$\r$\nClick Next to continue."
!insertmacro MUI_PAGE_WELCOME
!define MUI_LICENSEPAGE_TEXT_TOP "${APP} is free software under the GNU GPL, version 2."
!define MUI_LICENSEPAGE_BUTTON "&Next >"
!define MUI_LICENSEPAGE_TEXT_BOTTOM "You don't have to accept the license to use ${APP}; it covers copying and changing it. Click Next to continue."
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!define MUI_COMPONENTSPAGE_SMALLDESC
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE CheckFolder
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TITLE "${APP} is installed"
!define MUI_FINISHPAGE_TEXT "${APP} is ready. Pick what happens next, then click Finish."
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Open ${APP} now"
!define MUI_FINISHPAGE_RUN_FUNCTION OpenSable
; the readme checkbox is MUI2's second finish-page option, used here for the
; default app instead
!define MUI_FINISHPAGE_SHOWREADME
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Make ${APP} my default PDF app"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION MakeDefault
!define MUI_FINISHPAGE_LINK "${APP} on GitHub"
!define MUI_FINISHPAGE_LINK_LOCATION "${URL}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; a running Sable.exe can't be opened for writing, so this tells us whether
; files can be replaced
!macro WaitForClosedSable un
	Function ${un}WaitForClosedSable
		${DoWhile} ${FileExists} "$INSTDIR\${EXE}"
			ClearErrors
			FileOpen $0 "$INSTDIR\${EXE}" a
			${IfNot} ${Errors}
				FileClose $0
				${Break}
			${EndIf}
			MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "${APP} is open. Close every ${APP} window, then click Retry." /SD IDCANCEL IDRETRY +2
			Abort
		${Loop}
	FunctionEnd
!macroend
!insertmacro WaitForClosedSable ""
!insertmacro WaitForClosedSable "un."

Function .onInit
	${If} ${ARCH} == "ARM64"
		${IfNot} ${IsNativeARM64}
			MessageBox MB_ICONSTOP "This is the ARM64 build of ${APP}. Download the x64 installer for this PC." /SD IDOK
			Abort
		${EndIf}
	${ElseIfNot} ${RunningX64}
		MessageBox MB_ICONSTOP "${APP} needs 64-bit Windows." /SD IDOK
		Abort
	${EndIf}
	SetRegView 64
	SetShellVarContext current
FunctionEnd

Function .onVerifyInstDir
	; a drive root or Windows itself is never a good place
	${GetRoot} "$INSTDIR" $0
	${If} "$INSTDIR" == "$0"
	${OrIf} "$INSTDIR" == "$0\"
	${OrIf} "$INSTDIR" == "$WINDIR"
		Abort
	${EndIf}
FunctionEnd

; setup isn't elevated, so a folder like Program Files can't be written to
Function CheckFolder
	ClearErrors
	CreateDirectory "$INSTDIR"
	FileOpen $0 "$INSTDIR\.sable-setup-check" w
	${If} ${Errors}
		MessageBox MB_ICONEXCLAMATION "${APP} can't be installed in $INSTDIR because that folder needs administrator rights.$\r$\n$\r$\nPick a folder in your user folder or on another drive." /SD IDOK
		Abort
	${EndIf}
	FileClose $0
	Delete "$INSTDIR\.sable-setup-check"
FunctionEnd

Function CheckRuntime
	nsExec::ExecToStack `powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "if (Get-AppxPackage -Name 'Microsoft.WindowsAppRuntime.2' -PackageTypeFilter Framework | Where-Object { $$_.Architecture -eq '${RUNTIME_ARCH}' -and [version]$$_.Version -ge [version]'${RUNTIME_MIN}' }) { exit 0 } else { exit 1 }"`
	Pop $0
	Pop $1
	${If} $0 == 0
		StrCpy $RuntimeMissing 0
	${Else}
		StrCpy $RuntimeMissing 1
	${EndIf}
FunctionEnd

Function InstallRuntime
	DetailPrint "Downloading Microsoft's Windows App Runtime..."
	InitPluginsDir
	nsExec::ExecToStack `powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$$ProgressPreference = 'SilentlyContinue'; Invoke-WebRequest -UseBasicParsing -Uri '${RUNTIME_URL}' -OutFile '$PLUGINSDIR\WindowsAppRuntimeInstall.exe'"`
	Pop $0
	Pop $1
	${IfNot} ${FileExists} "$PLUGINSDIR\WindowsAppRuntimeInstall.exe"
		DetailPrint "The download didn't work: $1"
		MessageBox MB_YESNO|MB_ICONEXCLAMATION "${APP} needs Microsoft's Windows App Runtime 2.5, and it couldn't be downloaded. Check your internet connection.$\r$\n$\r$\nOpen Microsoft's download page now? ${APP} will start once the runtime is installed." /SD IDNO IDNO +2
		ExecShell "open" "${RUNTIME_PAGE}"
		Return
	${EndIf}
	DetailPrint "Installing Microsoft's Windows App Runtime..."
	; see: https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/deploy-unpackaged-apps
	ExecWait `"$PLUGINSDIR\WindowsAppRuntimeInstall.exe" --quiet` $0
	; without elevation it installs for this user only, and Microsoft's page
	; lists a code for that, so the result is checked rather than trusted
	Push $0
	Call CheckRuntime
	Pop $0
	${If} $RuntimeMissing == 0
		StrCpy $RuntimeMissing 0
		DetailPrint "Windows App Runtime installed."
	${Else}
		DetailPrint "The Windows App Runtime installer stopped with code $0."
		MessageBox MB_YESNO|MB_ICONEXCLAMATION "Microsoft's Windows App Runtime didn't install (code $0).$\r$\n$\r$\nOpen Microsoft's download page to install it yourself?" /SD IDNO IDNO +2
		ExecShell "open" "${RUNTIME_PAGE}"
	${EndIf}
FunctionEnd

; an earlier install is removed with its own uninstaller first, so its
; files go even when the new version doesn't have them, and only one Sable
; stays registered
Function RemoveEarlierInstall
	ReadRegStr $0 HKCU "${UNINSTALL_KEY}" "InstallLocation"
	${If} $0 != ""
	${AndIf} ${FileExists} "$0\${UNINSTALLER}"
		DetailPrint "Removing the earlier version from $0..."
		ExecWait `"$0\${UNINSTALLER}" /S _?=$0`
		Delete "$0\${UNINSTALLER}"
		RMDir "$0"
	${EndIf}
FunctionEnd

Section "${APP}" SecApp
	SectionIn RO
	Call WaitForClosedSable
	Call RemoveEarlierInstall
	SetOutPath "$INSTDIR"
	File /r "${SOURCE}\*.*"
	WriteUninstaller "$INSTDIR\${UNINSTALLER}"

	; Open with and Default apps
	; see: https://learn.microsoft.com/en-us/windows/win32/shell/default-programs
	WriteRegStr HKCU "Software\Classes\${PROGID}" "" "PDF Document"
	WriteRegStr HKCU "Software\Classes\${PROGID}" "FriendlyTypeName" "PDF Document"
	WriteRegStr HKCU "Software\Classes\${PROGID}\DefaultIcon" "" "$INSTDIR\${EXE},0"
	WriteRegStr HKCU "Software\Classes\${PROGID}\Application" "ApplicationName" "${APP}"
	WriteRegStr HKCU "Software\Classes\${PROGID}\Application" "ApplicationIcon" "$INSTDIR\${EXE},0"
	WriteRegStr HKCU "Software\Classes\${PROGID}\Application" "ApplicationCompany" "${PUBLISHER}"
	WriteRegStr HKCU "Software\Classes\${PROGID}\shell\open\command" "" `"$INSTDIR\${EXE}" "%1"`
	WriteRegStr HKCU "Software\Classes\.pdf\OpenWithProgids" "${PROGID}" ""
	WriteRegStr HKCU "Software\Classes\Applications\${EXE}" "FriendlyAppName" "${APP}"
	WriteRegStr HKCU "Software\Classes\Applications\${EXE}\DefaultIcon" "" "$INSTDIR\${EXE},0"
	WriteRegStr HKCU "Software\Classes\Applications\${EXE}\SupportedTypes" ".pdf" ""
	WriteRegStr HKCU "Software\Classes\Applications\${EXE}\shell\open\command" "" `"$INSTDIR\${EXE}" "%1"`
	WriteRegStr HKCU "${CAPABILITIES}" "ApplicationName" "${APP}"
	WriteRegStr HKCU "${CAPABILITIES}" "ApplicationDescription" "A dark-mode PDF reader"
	WriteRegStr HKCU "${CAPABILITIES}" "ApplicationIcon" "$INSTDIR\${EXE},0"
	WriteRegStr HKCU "${CAPABILITIES}\FileAssociations" ".pdf" "${PROGID}"
	WriteRegStr HKCU "Software\RegisteredApplications" "${APP}" "${CAPABILITIES}"
	WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\${EXE}" "" "$INSTDIR\${EXE}"
	WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\${EXE}" "Path" "$INSTDIR"

	; Installed apps
	WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP}"
	WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
	WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${EXE},0"
	WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
	WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "${URL}"
	WriteRegStr HKCU "${UNINSTALL_KEY}" "HelpLink" "${URL}/issues"
	WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
	WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" `"$INSTDIR\${UNINSTALLER}"`
	WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" `"$INSTDIR\${UNINSTALLER}" /S`
	WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
	WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
	${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
	IntFmt $0 "0x%08X" $0
	WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" $0

	; SHCNE_ASSOCCHANGED, SHCNF_DWORD | SHCNF_FLUSH
	System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0x1003, p 0, p 0)'
	Sleep 1000

	Call CheckRuntime
	${If} $RuntimeMissing == 1
		Call InstallRuntime
	${EndIf}
SectionEnd

Section "Start menu shortcut" SecStart
	CreateShortcut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0 "" "" "A dark-mode PDF reader"
SectionEnd

Section /o "Desktop shortcut" SecDesktop
	CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0 "" "" "A dark-mode PDF reader"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${SecApp} "${APP} itself, and its place in Open with and Default apps for PDFs."
	!insertmacro MUI_DESCRIPTION_TEXT ${SecStart} "Adds ${APP} to the Start menu, so you can search for it and pin it."
	!insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Puts a ${APP} shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function OpenSable
	Exec `"$INSTDIR\${EXE}"`
FunctionEnd

; Windows doesn't let a program set itself as the default. Settings'
; per-app page (registeredAppUser) only found machine-wide installs when
; tried, and Windows 11 answers SHOpenWithDialog's register mode with "go to
; Settings", so this says what to pick and opens Default apps, where .pdf
; lists Sable.
; see: https://learn.microsoft.com/en-us/windows/apps/develop/launch/launch-settings-app
Function MakeDefault
	MessageBox MB_OK|MB_ICONINFORMATION "Windows lets only you choose your PDF app.$\r$\n$\r$\nIn the Settings page that opens, type .pdf in the box at the top, click the app shown under it and choose ${APP}." /SD IDOK
	ExecShell "open" "ms-settings:defaultapps"
FunctionEnd

Function un.onInit
	SetRegView 64
	SetShellVarContext current
FunctionEnd

Section "Uninstall"
	Call un.WaitForClosedSable
	!include "${UNINSTALL_LIST}"
	Delete "$INSTDIR\${UNINSTALLER}"
	RMDir "$INSTDIR"

	Delete "$SMPROGRAMS\${APP}.lnk"
	Delete "$DESKTOP\${APP}.lnk"

	DeleteRegValue HKCU "Software\Classes\.pdf\OpenWithProgids" "${PROGID}"
	DeleteRegKey HKCU "Software\Classes\${PROGID}"
	DeleteRegKey HKCU "Software\Classes\Applications\${EXE}"
	DeleteRegValue HKCU "Software\RegisteredApplications" "${APP}"
	DeleteRegKey HKCU "Software\Sable"
	DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\${EXE}"
	DeleteRegKey HKCU "${UNINSTALL_KEY}"

	; SHCNE_ASSOCCHANGED, SHCNF_DWORD | SHCNF_FLUSH
	System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0x1003, p 0, p 0)'
	Sleep 1000
	; settings and logs stay in AppData, so a reinstall picks them up
SectionEnd

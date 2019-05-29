# Use the modern UI
!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!include "MultiUser.nsh"
!include "MUI2.nsh"
!include ".\tools\EnvVarUpdate.nsh"

# Basics
Name "Janet"
OutFile "janet-installer.exe"

# Some Configuration
!define APPNAME "Janet"
!define DESCRIPTION "The Janet Programming Language"
!define HELPURL "http://janet-lang.org"

# MUI Configuration
!define MUI_ICON "assets\icon.ico"
!define MUI_UNICON "assets\icon.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "assets\janet-w200.png"
!define MUI_HEADERIMAGE_RIGHT

# Show a welcome page first
!insertmacro MUI_PAGE_WELCOME

# License page
!insertmacro MUI_PAGE_LICENSE "LICENSE"

# Pick Install Directory
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_DIRECTORY

page instfiles

# Need to set a language.
!insertmacro MUI_LANGUAGE "English"
 
function .onInit
	setShellVarContext all
functionEnd

section "install"
    createDirectory "$INSTDIR\Library"
    createDirectory "$INSTDIR\C"
    createDirectory "$INSTDIR\bin"
	setOutPath $INSTDIR
    
    file /oname=bin\janet.exe dist\janet.exe
    file /oname=logo.ico assets\icon.ico
    
    file /oname=Library\cook.janet dist\cook.janet
    
    file /oname=C\janet.h dist\janet.h
    file /oname=C\janetconf.h dist\janetconf.h
    file /oname=C\janet.lib dist\janet.lib
    file /oname=C\janet.exp dist\janet.exp
    file /oname=C\janet.c dist\janet.c
    
    file /oname=bin\jpm.janet dist\jpm
    file /oname=bin\jpm.bat dist\jpm.bat
 
	# Uninstaller - See function un.onInit and section "uninstall" for configuration
	writeUninstaller "$INSTDIR\uninstall.exe"
 
	# Start Menu
	createShortCut "$SMPROGRAMS\Janet.lnk" "$INSTDIR\janet.exe" "" "$INSTDIR\logo.ico"
    
    # HKLM (all users) vs HKCU (current user)
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" JANET_PATH "$INSTDIR\Library"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" JANET_HEADERPATH "$INSTDIR\C"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" JANET_BINDIR "$INSTDIR\bin"

    WriteRegExpandStr HKCU "Environment" JANET_PATH "$INSTDIR\Library"
    WriteRegExpandStr HKCU "Environment" JANET_HEADERPATH "$INSTDIR\C"
    WriteRegExpandStr HKCU "Environment" JANET_BINDIR "$INSTDIR\bin"

    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
    
    # Update path
    ${EnvVarUpdate} $0 "PATH" "A" "HKCU" "$INSTDIR\bin" ; Append
    ${EnvVarUpdate} $0 "PATH" "A" "HKLM" "$INSTDIR\bin" ; Append  
 
	# Registry information for add/remove programs
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "DisplayName" "Janet"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "UninstallString" "$INSTDIR\uninstall.exe"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "QuietUninstallString" "$INSTDIR\uninstall.exe /S"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "InstallLocation" "$INSTDIR"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "DisplayIcon" "$INSTDIR\logo.ico"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "Publisher" "Janet-Lang.org"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "HelpLink" "${HELPURL}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "URLUpdateInfo" "${HELPURL}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "URLInfoAbout" "${HELPURL}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "DisplayVersion" "0.6.0"
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "VersionMajor" 0
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "VersionMinor" 6
	# There is no option for modifying or repairing the install
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "NoModify" 1
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "NoRepair" 1
	# Set the INSTALLSIZE constant (!defined at the top of this script) so Add/Remove Programs can accurately report the size
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet" "EstimatedSize" 1000
sectionEnd
 
# Uninstaller
 
function un.onInit
	SetShellVarContext all
 
	#Verify the uninstaller - last chance to back out
	MessageBox MB_OKCANCEL "Permanantly remove Janet?" IDOK next
		Abort
	next:
functionEnd
 
section "uninstall"
 
	# Remove Start Menu launcher
	delete "$SMPROGRAMS\Janet.lnk"
 
	# Remove files
    delete $INSTDIR\logo.ico
    
    delete $INSTDIR\C\janet.c
    delete $INSTDIR\C\janet.h
    delete $INSTDIR\C\janet.lib
    delete $INSTDIR\C\janet.exp
    delete $INSTDIR\C\janetconf.h
    
    delete $INSTDIR\bin\jpm.janet
    delete $INSTDIR\bin\jpm.bat
    delete $INSTDIR\bin\janet.exe

    delete $INSTDIR\Library\cook.janet
    
    # Remove env vars

    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" JANET_PATH
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" JANET_HEADERPATH
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" JANET_BINDIR

    DeleteRegValue HKCU "Environment" JANET_PATH
    DeleteRegValue HKCU "Environment" JANET_HEADERPATH
    DeleteRegValue HKCU "Environment" JANET_BINDIR

    # Unset PATH
    ${un.EnvVarUpdate} $0 "PATH" "R" "HKCU" "$INSTDIR\bin" ; Remove
    ${un.EnvVarUpdate} $0 "PATH" "R" "HKLM" "$INSTDIR\bin" ; Remove 
    
    # make sure windows knows about the change
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
     
	# Always delete uninstaller as the last action
	delete $INSTDIR\uninstall.exe
 
    rmDir "$INSTDIR\Library"
	rmDir "$INSTDIR\C"
    rmDir "$INSTDIR\bin"
 
	# Remove uninstaller information from the registry
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet"
sectionEnd
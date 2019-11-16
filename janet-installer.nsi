# Version
!define VERSION "1.5.1"
!define PRODUCT_VERSION "${VERSION}.0"
VIProductVersion "${PRODUCT_VERSION}"
VIFileVersion "${PRODUCT_VERSION}"

# Use the modern UI
!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY "Software\Janet\${VERSION}"
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME ""
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "Software\Janet\${VERSION}"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME ""
!define MULTIUSER_INSTALLMODE_INSTDIR "Janet-${VERSION}"

# For now, use 32 bit folder as build is 32 bit
# !define MULTIUSER_USE_PROGRAMFILES64

# Includes
!include "MultiUser.nsh"
!include "MUI2.nsh"
!include ".\tools\EnvVarUpdate.nsh"
!include "LogicLib.nsh"

# Basics
Name "Janet"

# Do some NSIS-fu to figure out at compile time if we are in appveyor
!define OUTNAME $%APPVEYOR_REPO_TAG_NAME%
!define "CHECK_${OUTNAME}"
!define DOLLAR "$"
!ifdef CHECK_${DOLLAR}%APPVEYOR_REPO_TAG_NAME%
    # We are not in the appveyor environment, use version name
    !define OUTNAME_PART v${VERSION}
!else
    # We are in appveyor, use git tag name for installer
    !define OUTNAME_PART ${OUTNAME}
!endif
OutFile "janet-${OUTNAME_PART}-windows-installer.exe"

# Some Configuration
!define APPNAME "Janet"
!define DESCRIPTION "The Janet Programming Language"
!define HELPURL "http://janet-lang.org"
BrandingText "The Janet Programming Language"

# Macros for setting registry values
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Janet-${VERSION}"
!macro WriteEnv key value
    ${If} $MultiUser.InstallMode == "AllUsers"
        WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "${key}" "${value}"
    ${Else}
        WriteRegExpandStr HKCU "Environment" "${key}" "${value}"
    ${EndIf}
!macroend
!macro DelEnv key
    ${If} $MultiUser.InstallMode == "AllUsers"
        DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "${key}"
    ${Else}
        DeleteRegValue HKCU "Environment" "${key}"
    ${EndIf}
!macroend

# MUI Configuration
!define MUI_ICON "assets\icon.ico"
!define MUI_UNICON "assets\icon.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "assets\janet-w200.png"
!define MUI_HEADERIMAGE_RIGHT
!define MUI_ABORTWARNING

# Show a welcome page first
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"

# Pick Install Directory
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

# Done
!insertmacro MUI_PAGE_FINISH

# Need to set a language.
!insertmacro MUI_LANGUAGE "English"

function .onInit
    !insertmacro MULTIUSER_INIT
functionEnd

section "Janet" BfWSection

    createDirectory "$INSTDIR\Library"
    createDirectory "$INSTDIR\C"
    createDirectory "$INSTDIR\bin"
    createDirectory "$INSTDIR\docs"
    setOutPath "$INSTDIR"

    # Bin files
    file /oname=bin\janet.exe dist\janet.exe
    file /oname=logo.ico assets\icon.ico
    file /oname=bin\jpm.janet auxbin\jpm
    file /oname=bin\jpm.bat tools\jpm.bat

    # C headers and library files
    file /oname=C\janet.h dist\janet.h
    file /oname=C\janetconf.h dist\janetconf.h
    file /oname=C\janet.lib dist\janet.lib
    file /oname=C\janet.exp dist\janet.exp
    file /oname=C\janet.c dist\janet.c
    file /oname=C\libjanet.lib dist\libjanet.lib

    # Documentation
    file /oname=docs\docs.html dist\doc.html

    # Other
    file README.md
    file LICENSE

    # Uninstaller - See function un.onInit and section "uninstall" for configuration
    writeUninstaller "$INSTDIR\uninstall.exe"

    # Start Menu
    createShortCut "$SMPROGRAMS\Janet.lnk" "$INSTDIR\bin\janet.exe" "" "$INSTDIR\logo.ico"

    # Set up Environment variables
    !insertmacro WriteEnv JANET_PATH "$INSTDIR\Library"
    !insertmacro WriteEnv JANET_HEADERPATH "$INSTDIR\C"
    !insertmacro WriteEnv JANET_LIBPATH "$INSTDIR\C"
    !insertmacro WriteEnv JANET_BINPATH "$INSTDIR\bin"

    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000

    # Update path
    ${If} $MultiUser.InstallMode == "AllUsers"
        ${EnvVarUpdate} $0 "PATH" "A" "HKLM" "$INSTDIR\bin" ; Append
    ${Else}
        ${EnvVarUpdate} $0 "PATH" "A" "HKCU" "$INSTDIR\bin" ; Append
    ${EndIf}

    # Registry information for add/remove programs
    WriteRegStr SHCTX "${UNINST_KEY}" "DisplayName" "Janet"
    WriteRegStr SHCTX "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr SHCTX "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\logo.ico"
    WriteRegStr SHCTX "${UNINST_KEY}" "Publisher" "Janet-Lang.org"
    WriteRegStr SHCTX "${UNINST_KEY}" "HelpLink" "${HELPURL}"
    WriteRegStr SHCTX "${UNINST_KEY}" "URLUpdateInfo" "${HELPURL}"
    WriteRegStr SHCTX "${UNINST_KEY}" "URLInfoAbout" "${HELPURL}"
    WriteRegStr SHCTX "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegDWORD SHCTX "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD SHCTX "${UNINST_KEY}" "NoRepair" 1
    WriteRegDWORD SHCTX "${UNINST_KEY}" "EstimatedSize" 1000
    # Add uninstall
    WriteRegStr SHCTX "${UNINST_KEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\" /$MultiUser.InstallMode"
    WriteRegStr SHCTX "${UNINST_KEY}" "QuietUninstallString" "$\"$INSTDIR\uninstall.exe$\" /$MultiUser.InstallMode /S"

sectionEnd

# Uninstaller

function un.onInit
    !insertmacro MULTIUSER_UNINIT
functionEnd

section "uninstall"

    # Remove Start Menu launcher
    delete "$SMPROGRAMS\Janet.lnk"

    # Remove files
    delete "$INSTDIR\logo.ico"
    delete "$INSTDIR\README.md"
    delete "$INSTDIR\LICENSE"
    rmdir /r "$INSTDIR\Library"
    rmdir /r "$INSTDIR\bin"
    rmdir /r "$INSTDIR\C"
    rmdir /r "$INSTDIR\docs"

    # Remove env vars
    !insertmacro DelEnv JANET_PATH
    !insertmacro DelEnv JANET_HEADERPATH
    !insertmacro DelEnv JANET_LIBPATH
    !insertmacro DelEnv JANET_BINPATH

    # Unset PATH
    ${If} $MultiUser.InstallMode == "AllUsers"
        ${un.EnvVarUpdate} $0 "PATH" "R" "HKLM" "$INSTDIR\bin" ; Remove
    ${Else}
        ${un.EnvVarUpdate} $0 "PATH" "R" "HKCU" "$INSTDIR\bin" ; Remove
    ${EndIf}

    # make sure windows knows about the change
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000

    # Always delete uninstaller as the last action
    delete "$INSTDIR\uninstall.exe"

    # Remove uninstaller information from the registry
    DeleteRegKey SHCTX "${UNINST_KEY}"

sectionEnd

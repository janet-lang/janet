!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!define MULTIUSER_INSTALLMODE_INSTDIR "janet"
!include "MultiUser.nsh"
!include "MUI2.nsh"
 
Name "Janet"
OutFile "janet-install.exe"
 
!define MUI_ABORTWARNING
 
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_DIRECTORY

!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
 
!insertmacro MUI_LANGUAGE "English"
 
Section "Janet" BfWSection
  SetOutPath $INSTDIR
  File "janet.exe"
  WriteUninstaller "$INSTDIR\janet-uninstall.exe"
SectionEnd

Function .onInit
  !insertmacro MULTIUSER_INIT
  !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${BfWSection} "The Janet programming language."
!insertmacro MUI_FUNCTION_DESCRIPTION_END
 
Section "Uninstall"
  Delete "$INSTDIR\janet.exe"
  Delete "$INSTDIR\janet-uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
 
Function un.onInit
  !insertmacro MULTIUSER_UNINIT
  !insertmacro MUI_UNGETLANGUAGE
FunctionEnd
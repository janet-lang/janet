@rem Build janet on windows
@rem
@rem Open a "Windows SDK Command Shell" and cd to the janet directory
@rem Then run this script with no arguments to build the executable

@echo off

@rem Ensure correct command prompt
@if not defined INCLUDE goto :BADCMD

@rem Sub commands
@if "%1"=="help" goto HELP
@if "%1"=="clean" goto CLEAN
@if "%1"=="test" goto TEST
@if "%1"=="dist" goto DIST
@if "%1"=="install" goto INSTALL
@if "%1"=="all" goto ALL

@rem Set compile and link options here
@setlocal

@rem Example use asan
@rem set JANET_COMPILE=cl /nologo /Isrc\include /Isrc\conf /c /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /MD /fsanitize=address /Zi
@rem set JANET_LINK=link /nologo clang_rt.asan_dynamic-x86_64.lib clang_rt.asan_dynamic_runtime_thunk-x86_64.lib

@set JANET_COMPILE=cl /nologo /Isrc\include /Isrc\conf /c /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /MD
@set JANET_LINK=link /nologo

@set JANET_LINK_STATIC=lib /nologo

@rem Add janet build tag
if not "%JANET_BUILD%" == "" (
    @set JANET_COMPILE=%JANET_COMPILE% /DJANET_BUILD="\"%JANET_BUILD%\""
)

if not exist build mkdir build
if not exist build\core mkdir build\core
if not exist build\c mkdir build\c
if not exist build\boot mkdir build\boot

@rem Build the bootstrap interpreter
for %%f in (src\core\*.c) do (
    %JANET_COMPILE% /DJANET_BOOTSTRAP /Fobuild\boot\%%~nf.obj %%f
    @if errorlevel 1 goto :BUILDFAIL
)
for %%f in (src\boot\*.c) do (
    %JANET_COMPILE% /DJANET_BOOTSTRAP /Fobuild\boot\%%~nf.obj %%f
    @if errorlevel 1 goto :BUILDFAIL
)
%JANET_LINK% /out:build\janet_boot.exe build\boot\*.obj
@if errorlevel 1 goto :BUILDFAIL
build\janet_boot . > build\c\janet.c
@if errorlevel 1 goto :BUILDFAIL

@rem Build the sources
%JANET_COMPILE% /Fobuild\janet.obj build\c\janet.c
@if errorlevel 1 goto :BUILDFAIL
%JANET_COMPILE% /Fobuild\shell.obj src\mainclient\shell.c
@if errorlevel 1 goto :BUILDFAIL

@rem Build the resources
rc /nologo /fobuild\janet_win.res janet_win.rc
@if errorlevel 1 goto :BUILDFAIL

@rem Link everything to main client
%JANET_LINK% /out:janet.exe build\janet.obj build\shell.obj build\janet_win.res
@if errorlevel 1 goto :BUILDFAIL

@rem Build static library (libjanet.lib)
%JANET_LINK_STATIC% /out:build\libjanet.lib build\janet.obj
@if errorlevel 1 goto :BUILDFAIL

echo === Successfully built janet.exe for Windows ===
echo === Run 'build_win test' to run tests. ==
echo === Run 'build_win clean' to delete build artifacts. ===
exit /b 0

@rem Not using correct command line
:BADCMD
@echo You must open a "Visual Studio .NET Command Prompt" to run this script
exit /b 1

@rem Show help
:HELP
@echo.
@echo Usage: build_windows [subcommand=clean,help,test,dist]
@echo.
@echo Script to build janet on windows. Must be run from the Visual Studio
@echo command prompt.
exit /b 0

@rem Clean build artifacts
:CLEAN
del *.exe *.lib *.exp *.msi *.wixpdb
rd /s /q build
if exist dist (
    rd /s /q dist
)
exit /b 0

@rem Run tests
:TEST
for %%f in (test/suite*.janet) do (
    janet.exe test\%%f
    @if errorlevel 1 goto TESTFAIL
)
exit /b 0

@rem Build a dist directory
:DIST
mkdir dist
janet.exe tools\gendoc.janet > dist\doc.html
janet.exe tools\removecr.janet dist\doc.html
janet.exe tools\removecr.janet build\c\janet.c

copy build\c\janet.c dist\janet.c
copy src\mainclient\shell.c dist\shell.c
copy janet.exe dist\janet.exe
copy LICENSE dist\LICENSE
copy README.md dist\README.md

copy janet.lib dist\janet.lib
copy janet.exp dist\janet.exp

janet.exe tools\patch-header.janet src\include\janet.h src\conf\janetconf.h build\janet.h
copy build\janet.h dist\janet.h
copy build\libjanet.lib dist\libjanet.lib

@rem Create installer
janet.exe -e "(->> janet/version (peg/match ''(* :d+ `.` :d+ `.` :d+)) first print)" > build\version.txt
janet.exe -e "(print (os/arch))" > build\arch.txt
set /p JANET_VERSION= < build\version.txt
set /p BUILDARCH= < build\arch.txt
echo "JANET_VERSION is %JANET_VERSION%"
if defined APPVEYOR_REPO_TAG_NAME (
    set RELEASE_VERSION=%APPVEYOR_REPO_TAG_NAME%
) else (
    set RELEASE_VERSION=%JANET_VERSION%
)
if defined CI (
    set WIXBIN="%WIX%bin\"
    echo WIXBIN = %WIXBIN%
) else (
    set WIXBIN=
)

set WIXARCH=%BUILDARCH%
if "%WIXARCH%"=="aarch64" (
    set WIXARCH=arm64
)

%WIXBIN%candle.exe tools\msi\janet.wxs -arch %WIXARCH% -out build\
%WIXBIN%light.exe "-sice:ICE38" -b tools\msi -ext WixUIExtension build\janet.wixobj -out janet-%RELEASE_VERSION%-windows-%BUILDARCH%-installer.msi
exit /b 0

@rem Run the installer. (Installs to the local user with default settings)
:INSTALL
FOR %%a in (janet-*-windows-*-installer.msi) DO (
    @echo Running Installer %%a...
    %%a /QN
)
exit /b 0

@rem build, test, dist, install. Useful for local dev.
:ALL
call %0 build
@if errorlevel 1 exit /b 1
call %0 test
@if errorlevel 1 exit /b 1
call %0 dist
@if errorlevel 1 exit /b 1
call %0 install
@if errorlevel 1 exit /b 1
@echo Done!
exit /b 0

:TESTFAIL
@echo.
@echo *******************************************************
@echo *** Tests FAILED -- Please check the error messages ***
@echo *******************************************************
exit /b 1

@rem Build failed
:BUILDFAIL
@echo.
@echo *******************************************************
@echo *** Build FAILED -- Please check the error messages ***
@echo *******************************************************
exit /b 1

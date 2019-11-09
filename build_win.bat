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
@if "%1"=="test-install" goto TESTINSTALL
@if "%1"=="all" goto ALL

@rem Set compile and link options here
@setlocal
@set JANET_COMPILE=cl /nologo /Isrc\include /Isrc\conf /c /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /MD
@set JANET_LINK=link /nologo
@set JANET_LINK_STATIC=lib /nologo

@rem Add janet build tag
if not "%JANET_BUILD%" == "" (
    @set JANET_COMPILE=%JANET_COMPILE% /DJANET_BUILD="\"%JANET_BUILD%\""
)

mkdir build
mkdir build\core
mkdir build\mainclient
mkdir build\boot

@rem Build the xxd tool for generating sources
cl /nologo /c tools/xxd.c /Fobuild\xxd.obj
@if errorlevel 1 goto :BUILDFAIL
link /nologo /out:build\xxd.exe build\xxd.obj
@if errorlevel 1 goto :BUILDFAIL

@rem Generate the embedded sources
build\xxd.exe src\boot\boot.janet build\boot.gen.c janet_gen_boot
@if errorlevel 1 goto :BUILDFAIL

@rem Build the generated sources
%JANET_COMPILE% /Fobuild\boot\boot.gen.obj build\boot.gen.c
@if errorlevel 1 goto :BUILDFAIL

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
build\janet_boot build\core_image.c

@rem Build the core image
%JANET_COMPILE% /Fobuild\core_image.obj build\core_image.c
@if errorlevel 1 goto :BUILDFAIL

@rem Build the sources
for %%f in (src\core\*.c) do (
    %JANET_COMPILE% /Fobuild\core\%%~nf.obj %%f
    @if errorlevel 1 goto :BUILDFAIL
)

@rem Build the resources
rc /nologo /fobuild\janet_win.res janet_win.rc

@rem Build the main client
for %%f in (src\mainclient\*.c) do (
    %JANET_COMPILE% /Fobuild\mainclient\%%~nf.obj %%f
    @if errorlevel 1 goto :BUILDFAIL
)

@rem Link everything to main client
%JANET_LINK% /out:janet.exe build\core\*.obj build\mainclient\*.obj build\core_image.obj build\janet_win.res
@if errorlevel 1 goto :BUILDFAIL

@rem Build static library (libjanet.a)
%JANET_LINK_STATIC% /out:build\libjanet.lib build\core\*.obj build\core_image.obj
@if errorlevel 1 goto :BUILDFAIL

@rem Gen amlag
setlocal enabledelayedexpansion
set "amalg_files="
for %%f in (src\core\*.c) do (
    set "amalg_files=!amalg_files! %%f"
)
janet.exe tools\amalg.janet src\core\util.h src\core\state.h src\core\gc.h src\core\vector.h src\core\fiber.h src\core\regalloc.h src\core\compile.h src\core\emit.h src\core\symcache.h %amalg_files% build\core_image.c > build\janet.c
janet.exe tools\removecr.janet build\janet.c

@rem Gen shell.c
janet.exe tools\amalg.janet src\mainclient\line.h src\mainclient\line.c src\mainclient\main.c > build\shell.c
janet.exe tools\removecr.janet build\shell.c

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
del *.exe *.lib *.exp
rd /s /q build
rd /s /q dist
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

copy build\janet.c dist\janet.c
copy build\shell.c dist\shell.c
copy janet.exe dist\janet.exe
copy LICENSE dist\LICENSE
copy README.md dist\README.md

copy janet.lib dist\janet.lib
copy janet.exp dist\janet.exp

copy src\include\janet.h dist\janet.h
copy src\conf\janetconf.h dist\janetconf.h
copy build\libjanet.lib dist\libjanet.lib

copy auxbin\jpm dist\jpm
copy tools\jpm.bat dist\jpm.bat

@rem Create installer
"C:\Program Files (x86)\NSIS\makensis.exe" janet-installer.nsi
exit /b 0

@rem Run the installer. (Installs to the local user with default settings)
:INSTALL
@echo Running Installer...
FOR %%a in (janet-*-windows-installer.exe) DO (
    %%a /S /CurrentUser
)
exit /b 0

@rem Test the installation.
:TESTINSTALL
pushd test\install
call jpm clean
@if errorlevel 1 goto :TESTINSTALLFAIL
call jpm test
@if errorlevel 1 goto :TESTINSTALLFAIL
call jpm --verbose --modpath=. install https://github.com/janet-lang/json.git
@if errorlevel 1 goto :TESTINSTALLFAIL
call build\testexec
@if errorlevel 1 goto :TESTINSTALLFAIL
call jpm --verbose quickbin testexec.janet build\testexec2.exe
@if errorlevel 1 goto :TESTINSTALLFAIL
call build\testexec2.exe
@if errorlevel 1 goto :TESTINSTALLFAIL
call jpm --verbose --test --modpath=. install https://github.com/janet-lang/jhydro.git
@if errorlevel 1 goto :TESTINSTALLFAIL
call jpm --verbose --test --modpath=. install https://github.com/janet-lang/path.git
@if errorlevel 1 goto :TESTINSTALLFAIL
call jpm --verbose --test --modpath=. install https://github.com/janet-lang/argparse.git
@if errorlevel 1 goto :TESTINSTALLFAIL
popd
exit /b 0

:TESTINSTALLFAIL
popd
goto :TESTFAIL

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

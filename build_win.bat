@rem Build janet on windows
@rem
@rem Open a "Windows SDK Command Shell" and cd to the janet directory
@rem Then run this script with no arguments to build the executable

@echo off

@rem Ensure correct command prompt
@if not defined INCLUDE goto :BADCMD

@rem Set compile and link options here
@setlocal
@set JANET_COMPILE=cl /nologo /Isrc\include /c /O2 /W3 /LD /D_CRT_SECURE_NO_WARNINGS
@set JANET_LINK=link /nologo

@rem Sub commands
@if "%1"=="help" goto HELP
@if "%1"=="clean" goto CLEAN
@if "%1"=="test" goto TEST
@if "%1"=="dist" goto DIST

mkdir build
mkdir build\core
mkdir build\mainclient
mkdir build\boot

@rem Build the xxd tool for generating sources
@cl /nologo /c tools/xxd.c /Fobuild\xxd.obj
@if errorlevel 1 goto :BUILDFAIL
@link /nologo /out:build\xxd.exe build\xxd.obj
@if errorlevel 1 goto :BUILDFAIL

@rem Generate the embedded sources
@build\xxd.exe src\mainclient\init.janet build\init.gen.c janet_gen_init
@if errorlevel 1 goto :BUILDFAIL
@build\xxd.exe src\boot\boot.janet build\boot.gen.c janet_gen_boot
@if errorlevel 1 goto :BUILDFAIL

@rem Build the generated sources
@%JANET_COMPILE% /Fobuild\mainclient\init.gen.obj build\init.gen.c
@if errorlevel 1 goto :BUILDFAIL
@%JANET_COMPILE% /Fobuild\boot\boot.gen.obj build\boot.gen.c
@if errorlevel 1 goto :BUILDFAIL

@rem Build the bootstrap interpretter
for %%f in (src\core\*.c) do (
    @%JANET_COMPILE% /DJANET_BOOTSTRAP /Fobuild\boot\%%~nf.obj %%f
    @if errorlevel 1 goto :BUILDFAIL
)
for %%f in (src\boot\*.c) do (
    @%JANET_COMPILE% /DJANET_BOOTSTRAP /Fobuild\boot\%%~nf.obj %%f
    @if errorlevel 1 goto :BUILDFAIL
)
%JANET_LINK% /out:build\janet_boot.exe build\boot\*.obj
@if errorlevel 1 goto :BUILDFAIL
build\janet_boot build\core_image.c JANET_PATH "C:/Janet/Library"

@rem Build the core image
@%JANET_COMPILE% /Fobuild\core_image.obj build\core_image.c
@if errorlevel 1 goto :BUILDFAIL

@rem Build the sources
for %%f in (src\core\*.c) do (
    @%JANET_COMPILE% /Fobuild\core\%%~nf.obj %%f
    @if errorlevel 1 goto :BUILDFAIL
)

@rem Build the main client
for %%f in (src\mainclient\*.c) do (
    @%JANET_COMPILE% /Fobuild\mainclient\%%~nf.obj %%f
    @if errorlevel 1 goto :BUILDFAIL
)

@rem Link everything to main client
%JANET_LINK% /out:janet.exe build\core\*.obj build\mainclient\*.obj build\core_image.obj
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
del janet.exe janet.exp janet.lib
rd /s /q build
exit /b 0

@rem Run tests
:TEST
for %%f in (test/suite*.janet) do (
    janet.exe test\%%f
    @if errorlevel 1 goto :TESTFAIL
)
exit /b 0

@rem Build a dist directory
:DIST
mkdir dist
janet.exe tools\gendoc.janet > dist\doc.html

@rem Gen amlag
setlocal enabledelayedexpansion
set "amalg_files="
for %%f in (src\core\*.c) do (
    set "amalg_files=!amalg_files! %%f"
)
janet.exe tools\amalg.janet src\core\util.h src\core\state.h src\core\gc.h src\core\vector.h src\core\fiber.h src\core\regalloc.h src\core\compile.h src\core\emit.h src\core\symcache.h %amalg_files% build\core_image.c > dist\janet.c
copy janet.exe dist\janet.exe
copy LICENSE dist\LICENSE
copy README.md dist\README.md
copy janet.lib dist\janet.lib
copy janet.exp dist\janet.exp
copy src\include\janet.h dist\janet.h
copy src\include\janetconf.h dist\janetconf.h
copy tools\cook.janet dist\cook.janet
copy tools\highlight.janet dist\highlight.janet
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

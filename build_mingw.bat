@echo off
REM Build janet on Windows using GCC (MinGW)
REM 
REM Open a command prompt and cd to the janet directory
REM Then run this script with no arguments to build the executable

@if "%1"=="help" goto HELP
@if "%1"=="clean" goto CLEAN
@if "%1"=="test" goto TEST
@if "%1"=="dist" goto DIST
@if "%1"=="install" goto INSTALL
@if "%1"=="all" goto ALL

@setlocal

@rem Check if GCC is available
where gcc >nul 2>&1
if errorlevel 1 (
    @echo GCC not found. Please install MinGW-w64 and add it to your PATH.
    exit /b 1
)

@rem Compiler and linker settings
@set CC=g++
@set CFLAGS=-fpermissive -O2 -w -Isrc/include -Isrc/conf -D_CRT_SECURE_NO_WARNINGS -D_WIN32_WINNT=0x0601
@set LDFLAGS=-lwsock32 -lws2_32 -lmswsock -lpsapi -lshlwapi -luser32 -lkernel32 -ladvapi32

@rem Enable sanitizers if requested
if DEFINED SANITIZE (
    @set CFLAGS=%CFLAGS% -fsanitize=address
    @set LDFLAGS=%LDFLAGS% -fsanitize=address
)

@rem Add janet build tag
if not "%JANET_BUILD%" == "" (
    @set CFLAGS=%CFLAGS% -DJANET_BUILD="\"%JANET_BUILD%\""
)

@if "%1"=="fresh" goto FRESH

@rem Create build directories
if not exist build mkdir build
if not exist build\core mkdir build\core
if not exist build\boot mkdir build\boot
if not exist build\obj mkdir build\obj

@rem Build the bootstrap interpreter (JANET_BOOTSTRAP)
@echo Building bootstrap interpreter...
for %%f in (src\core\*.c) do (
    %CC% %CFLAGS% -DJANET_BOOTSTRAP -c %%f -o build\boot\%%~nf.o
    @if errorlevel 1 goto :BUILDFAIL
)
for %%f in (src\boot\*.c) do (
    %CC% %CFLAGS% -DJANET_BOOTSTRAP -c %%f -o build\boot\%%~nf.o
    @if errorlevel 1 goto :BUILDFAIL
)

@rem Link bootstrap with required Windows libraries
@echo Linking bootstrap...
%CC% -o build\janet_boot.exe build\boot\*.o %LDFLAGS%
@if errorlevel 1 goto :BUILDFAIL

@rem Generate janet.c from bootstrap interpreter
@echo Generating janet.c...
build\janet_boot.exe . > build\janet.c
@if errorlevel 1 goto :BUILDFAIL

@rem Build the main janet sources
@echo Building Janet core...
%CC% %CFLAGS% -c build\janet.c -o build\obj\janet.o
@if errorlevel 1 goto :BUILDFAIL

%CC% %CFLAGS% -c src\mainclient\shell.c -o build\obj\shell.o
@if errorlevel 1 goto :BUILDFAIL

@rem Link everything to main client
@echo Linking janet.exe...
%CC% -o janet.exe build\obj\janet.o build\obj\shell.o %LDFLAGS%
@if errorlevel 1 goto :BUILDFAIL

@rem Build static library (libjanet.a)
@echo Building static library...
ar rcs build\libjanet.a build\obj\janet.o
@if errorlevel 1 goto :BUILDFAIL

@echo.
@echo === Successfully built janet.exe for Windows using GCC ===
@echo === Run 'build_mingw test' to run tests. ===
@echo === Run 'build_mingw clean' to delete build artifacts. ===
exit /b 0

@rem Not using correct command line
:BADCMD
@echo You need to have GCC (MinGW-w64) in your PATH to run this script
exit /b 1

@rem Show help
:HELP
@echo.
@echo Usage: build_mingw [subcommand=clean,help,test,dist,install,all]
@echo.
@echo Script to build janet on windows using GCC (MinGW-w64).
@echo Must have GCC in your PATH.
@echo.
@echo Environment variables:
@echo   SANITIZE=1  - Enable address sanitizer
@echo   JANET_BUILD - Set build tag
exit /b 0

@rem Clean build artifacts
:CLEAN
@echo Cleaning build artifacts...
del janet.exe *.a *.exp 2>nul
rd /s /q build 2>nul
if exist dist (
    rd /s /q dist 2>nul
)
exit /b 0

@rem Run tests
:TEST
@echo Running tests...
for %%f in (test\suite*.janet) do (
    janet.exe %%f
    @if errorlevel 1 goto TESTFAIL
)
@echo All tests passed!
exit /b 0

@rem Build a dist directory
:DIST
@echo Building distribution...
if not exist dist mkdir dist

@rem Generate documentation
janet.exe tools\gendoc.janet > dist\doc.html
janet.exe tools\removecr.janet dist\doc.html
janet.exe tools\removecr.janet build\janet.c

@rem Copy files
copy build\janet.c dist\janet.c
copy src\mainclient\shell.c dist\shell.c
copy janet.exe dist\janet.exe
copy LICENSE dist\LICENSE
copy README.md dist\README.md

@rem Copy libraries
copy build\libjanet.a dist\libjanet.a 2>nul
copy janet.exp dist\janet.exp 2>nul

@rem Generate header
janet.exe tools\patch-header.janet src\include\janet.h src\conf\janetconf.h build\janet.h
copy build\janet.h dist\janet.h
copy build\libjanet.a dist\libjanet.a 2>nul

@echo Distribution built in dist directory
exit /b 0

@rem Run the installer (placeholder for GCC version)
:INSTALL
@echo Installing janet...
@echo Installation not implemented for GCC build. Please copy janet.exe manually.
@echo For Windows, you might want to use the MSVC build for proper installation.
exit /b 0

@rem build, test, dist, install. Useful for local dev.
:ALL
@echo Running full build cycle...
call %0 build
@if errorlevel 1 exit /b 1
call %0 test
@if errorlevel 1 exit /b 1
call %0 dist
@if errorlevel 1 exit /b 1
@echo Done! Build artifacts in dist directory.
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


@rem Build from strach
:FRESH
%CC% -o janet.exe src\core\*.c %CFLAGS% src\mainclient\shell-simple.c %LDFLAGS% -DJANET_BOOTSTRAP
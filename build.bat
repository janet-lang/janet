@rem Build dst on windows
@rem
@rem Open a "Windows SDK Command Shell" in the dst directory
@rem and run this script

@echo off

@rem Ensure correct command prompt
@if not defined INCLUDE goto :BADCMD

@rem Sub commands
@if "%1"=="help" goto HELP
@if "%1"=="clean" goto CLEAN
@if "%1"=="test" goto TEST

@rem Set compile and link options here
@setlocal
@set DST_COMPILE=cl /nologo /Isrc\include /c /O2 /W3 /D_CRT_SECURE_NO_WARNINGS
@set DST_LINK=link /nologo

@rem Build the xxd tool for generating sources
@cl /nologo /c src/tools/xxd.c /Foxxd.o
@if errorlevel 1 goto :BUILDFAIL
@link /nologo /out:xxd.exe xxd.o
@if errorlevel 1 goto :BUILDFAIL

@rem Generate the headers
@xxd.exe src/core/core.dst src/include/generated/core.h dst_gen_core
@if errorlevel 1 goto :BUILDFAIL
@xxd.exe src/mainclient/init.dst src/include/generated/init.h dst_gen_init
@if errorlevel 1 goto :BUILDFAIL

@rem Build the sources
for %%f in (src/core/*.c) do (
	@%DST_COMPILE% src/core/%%f
    @if errorlevel 1 goto :BUILDFAIL
)

@rem Build the main client
for %%f in (src/mainclient/*.c) do (
	@%DST_COMPILE% src/mainclient/%%f
    @if errorlevel 1 goto :BUILDFAIL
)

@rem Link everything to main client
%DST_LINK% /out:dst.exe *.obj
@if errorlevel 1 goto :BUILDFAIL

echo === Successfully built dst.exe for Windows ===
echo === Run 'build test' to run tests. ==
echo === Run 'build clean' to delete build artifacts. ===
exit /b 0

@rem Not using correct command line
:BADCMD
@echo You must open a "Visual Studio .NET Command Prompt" to run this script
exit /b 1

@rem Show help
:HELP
@echo.
@echo Usage: build_windows [subcommand=clean,help,test]
@echo.
@echo Script to build dst on windows. Must be run from the Visual Studio
@echo command prompt.
exit /b 0

@rem Clean build artifacts 
:CLEAN
del *.obj
del xxd.o
del xxd.exe
del dst.exe
del src\include\generated\*.h
exit /b 0

@rem Run tests
:TEST
for %%f in (test/suite*.dst) do (
	dst.exe test\%%f
	@if errorlevel 1 goto :TESTFAIL
)
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

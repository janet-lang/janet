@rem Build dst on windows
@rem
@rem Open a "Windows SDK Command Shell" and cd to the dst directory
@rem Then run this script with no arguments to build the executable

@echo off

@rem Ensure correct command prompt
@if not defined INCLUDE goto :BADCMD

@rem Subcommands
@if "%1"=="clean" goto CLEAN

@rem Set compile and link options here
@setlocal
@set DST_COMPILE=cl /nologo /I..\..\src\include /c /O2 /W3
@set DST_LINK=link /nologo /dll ..\..\dst.lib

@rem Build the sources
for %%f in (*.c) do (
	@%DST_COMPILE% %%f
    @if errorlevel 1 goto :BUILDFAIL
)

%DST_LINK% /out:hello.dll *.obj
@if errorlevel 1 goto :BUILDFAIL

echo === Successfully built hello.dll for Windows ===
echo === Run 'build clean' to delete build artifacts. ===
exit /b 0

@rem Not using correct command line
:BADCMD
@echo Use a Visual Studio Developer Command Prompt to run this script
exit /b 1

@rem Clean build artifacts 
:CLEAN
del *.obj
del hello.*
exit /b 0

@rem Build failed
:BUILDFAIL
@echo.
@echo *******************************************************
@echo *** Build FAILED -- Please check the error messages ***
@echo *******************************************************
exit /b 1

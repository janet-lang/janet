@rem Generated batch script, run in 'Visual Studio Developer Prompt'

@rem 

@echo off

cl /nologo /I..\..\src\include /c /O2 /W3 json.c
@if errorlevel 1 goto :BUILDFAIL

link /nologo /dll ..\..\janet.lib /out:json.dll *.obj
if errorlevel 1 goto :BUILDFAIL

@echo .
@echo ======
@echo Build Succeeded.
@echo =====
exit /b 0

:BUILDFAIL
@echo .
@echo =====
@echo BUILD FAILED. See Output For Details.
@echo =====
@echo .
exit /b 1

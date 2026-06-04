@echo off
setlocal
rem ============================================================
rem  Compile GDExtension for ProjectAbyss
rem ============================================================

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set VSLANG=1033

echo Building GDExtension (template_debug, x86_64)...
python run_scons.py %*

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Build failed. Use "CompileGameSource.bat --clean" for a full rebuild.
    exit /b %ERRORLEVEL%
)

echo.
echo Build complete: Game/bin/abyss.windows.template_debug.x86_64.dll
endlocal

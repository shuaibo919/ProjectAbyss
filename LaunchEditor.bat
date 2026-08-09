@echo off
setlocal

rem ============================================================
rem  Launch Godot Editor for ProjectAbysss
rem  Usage: launch_editor.bat [target] [arch] [extra_args...]
rem    target    - Build target (default: editor)
rem               Common values: editor, editor.dev
rem    arch      - Architecture (default: x86_64)
rem    extra_args - Additional arguments passed to Godot
rem  Examples:
rem    launch_editor.bat
rem    launch_editor.bat editor.dev
rem    launch_editor.bat editor.dev x86_64 --verbose
rem ============================================================

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=editor"

set "ARCH=%~2"
if "%ARCH%"=="" set "ARCH=x86_64"

rem Build paths relative to this script's location
set "SCRIPT_DIR=%~dp0"
set "BIN_DIR=%SCRIPT_DIR%Engine\bin"
set "PROJECT_DIR=%SCRIPT_DIR%Game"
set "GODOT_EXE=%BIN_DIR%\godot.windows.%TARGET%.%ARCH%.exe"

rem Check if the executable exists
if exist "%GODOT_EXE%" goto :found

echo [ERROR] Godot executable not found:
echo   %GODOT_EXE%
echo.
echo Available binaries in "%BIN_DIR%":
if not exist "%BIN_DIR%" (
    echo   bin directory does not exist. Please compile the engine first.
) else (
    dir /b "%BIN_DIR%\godot.windows.*.exe" 2>nul || echo   (none found)
)
echo.
echo Usage: %~nx0 [target] [arch]
echo   target: editor (default), editor.dev, template_debug, template_release
echo   arch:   x86_64 (default), x86_32, arm64
exit /b 1

:found
rem Collect extra arguments (from the 3rd parameter onward)
set "EXTRA_ARGS="
shift
shift
:parse_args
if "%~1"=="" goto :launch
set "EXTRA_ARGS=%EXTRA_ARGS% %1"
shift
goto :parse_args

:launch
echo Starting Godot Editor...
echo   Executable : %GODOT_EXE%
echo   Project    : %PROJECT_DIR%
echo.

"%GODOT_EXE%" --editor --verbose --path "%PROJECT_DIR%" %EXTRA_ARGS%

endlocal

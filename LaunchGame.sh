#!/bin/bash
# Launch ProjectAbysss Game — Git Bash wrapper
# Delegates to LaunchGame.bat via cmd.exe

if [[ "$(uname -o 2>/dev/null)" != "Msys" && "$(uname -s)" != *"MINGW"* && "$(uname -s)" != *"CYGWIN"* ]]; then
    echo "[ERROR] This project only supports development on Windows."
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -W)"
cmd.exe //C "$(cygpath -w "$SCRIPT_DIR/LaunchGame.bat")" "$@"

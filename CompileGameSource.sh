#!/bin/bash
# Run from git-bash. Avoids cmd.exe CP936 → mintty UTF-8 encoding breakage.
set -e
cd "$(dirname "$0")"
export VSLANG=1033
export PYTHONUTF8=1
python -X utf8 -m SCons "$@"

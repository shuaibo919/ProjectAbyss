#!/usr/bin/env python3
"""Wrapper that runs scons and fixes mixed-encoding output."""
import subprocess
import sys
import os

os.environ.setdefault('VSLANG', '1033')

proc = subprocess.run(
    [sys.executable, '-m', 'SCons'] + sys.argv[1:],
    capture_output=True
)

def decode_line(line: bytes) -> str:
    # GBK first: MSVC on Chinese Windows outputs GBK. ASCII (scons) is valid GBK too.
    try:
        return line.decode('gbk')
    except UnicodeDecodeError:
        pass
    try:
        return line.decode('utf-8')
    except UnicodeDecodeError:
        return line.decode('utf-8', errors='replace')

def decode_mixed(data: bytes) -> str:
    return '\n'.join(decode_line(l) for l in data.split(b'\n'))

sys.stdout.write(decode_mixed(proc.stdout))
sys.stderr.write(decode_mixed(proc.stderr))
sys.exit(proc.returncode)

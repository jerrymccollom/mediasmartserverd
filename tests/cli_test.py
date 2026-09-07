#!/usr/bin/env python3
"""CLI validation exits before lock acquisition or hardware access."""
import subprocess
import sys

binary = sys.argv[1]
cases = [
    (["--help"], 0), (["--version"], 0),
    (["--brightness=10"], 1), (["--brightness=-1"], 1),
    (["--brightness=bad"], 1), (["--brightness=18446744073709551616"], 1),
    (["--light-show=14"], 1), (["--usb=2"], 1),
    (["--model=unknown"], 1), (["--user="], 1), (["unexpected"], 1),
]
for arguments, expected in cases:
    result = subprocess.run([binary, *arguments], capture_output=True, text=True, timeout=3)
    assert result.returncode == expected, (arguments, result.returncode, result.stderr)
    assert "instance lock" not in result.stderr, (arguments, result.stderr)
    print("PASS CLI", " ".join(arguments))

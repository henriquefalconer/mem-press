#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
case "$(uname -s)" in
    MINGW*|MSYS*) ;;
    *) echo 'Run this test in native Windows Git Bash with MinGW GCC.' >&2; exit 1 ;;
esac
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT
trap 'exit 1' HUP INT TERM
# Leave TMP/TEMP untouched: GCC uses them for its own intermediate files.
gcc -O2 -Wall -Wextra -Werror -o "$build_dir/pressure-test.exe" tests/windows-pressure.c -lpsapi -lm
"$build_dir/pressure-test.exe"

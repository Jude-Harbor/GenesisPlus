#!/bin/sh
# Fails if any text file in the repository contains a carriage return (CRLF
# or CR line endings). Shell scripts and the Makefile break under Unix shells
# with CRLF, so this runs as part of `make test`.
#
# Uses `tr` rather than `grep`, because some Windows builds of grep hide
# carriage returns.

set -u
cd "$(dirname "$0")/.." || exit 2

bad=0
checked=0

for file in $(find . \
        \( -name .git -o -name build -o -name dist -o -name node_modules \) -prune -o \
        -type f \( -name '*.c' -o -name '*.h' -o -name '*.sh' -o -name '*.md' \
                -o -name '*.js' -o -name '*.json' -o -name '*.gns' -o -name '*.yml' \
                -o -name '*.svg' -o -name '*.expected' -o -name 'Makefile' -o -name 'LICENSE' \
                -o -name '.gitignore' -o -name '.gitattributes' -o -name '.vscodeignore' \) \
        -print); do
    checked=$((checked + 1))
    count=$(tr -d -c '\r' <"$file" | wc -c)
    if [ "$count" -ne 0 ]; then
        echo "CRLF/CR line endings in $file ($count carriage returns)"
        bad=$((bad + 1))
    fi
done

if [ "$bad" -ne 0 ]; then
    echo "Line-ending check FAILED: $bad file(s). Convert them to LF (see .gitattributes)."
    exit 1
fi

echo "Line-ending check: $checked text files, all LF."

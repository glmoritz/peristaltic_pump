#!/usr/bin/env bash
# Regenerate all CAD outputs (STL, STEP, FCStd, preview PNGs) into cad/out/.
# Usage: cad/build.sh [pump] [reservoir]   (default: both)
set -euo pipefail
FREECAD="${FREECAD:-$HOME/bin/FreeCAD_1.1.1-Linux-x86_64-py311.AppImage}"
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ $# -eq 0 ]]; then set -- pump reservoir; fi
for t in "$@"; do
    echo "### $t"
    # keep only the script's own report lines (FreeCAD's STEP writer is very chatty)
    CAD_TARGET="$t" "$FREECAD" freecadcmd "$here/run.py" 2>&1 \
        | grep -E '^(==|  )|Error|Traceback|Exception|File "' || true
done

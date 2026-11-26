#!/usr/bin/env bash
# Regenerate all CAD outputs into cad/out/ (STL, STEP, FCStd, previews), the
# perfboard layout drawing and the LEGO-style assembly instructions.
# Usage: cad/build.sh [pump] [reservoir] [electronics] [instructions]   (default: all)
set -euo pipefail
FREECAD="${FREECAD:-$HOME/bin/FreeCAD_1.1.1-Linux-x86_64-py311.AppImage}"
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ $# -eq 0 ]]; then set -- pump reservoir electronics instructions; fi
do_instructions=0
for t in "$@"; do
    if [[ "$t" == instructions ]]; then do_instructions=1; continue; fi
    echo "### $t"
    # keep only the script's own report lines (FreeCAD's STEP writer is very chatty)
    CAD_TARGET="$t" "$FREECAD" freecadcmd "$here/run.py" 2>&1 \
        | grep -E '^(==|  )|Error|Traceback|Exception|File "' || true
    if [[ "$t" == electronics ]]; then python3 "$here/pcb_layout.py"; fi
done
# Second pass in the FreeCAD GUI (off-screen): colours + saved camera for the
# assembly .FCStd files, which freecadcmd cannot store.
echo "### gui styling"
rm -f "$here/out/.gui_view.log"
QT_QPA_PLATFORM=offscreen "$FREECAD" "$here/gui_view.py" >/dev/null 2>&1 || true
cat "$here/out/.gui_view.log" 2>/dev/null || echo "  (gui styling step produced no log)"
if [[ $do_instructions == 1 ]]; then
    echo "### instructions"
    QT_QPA_PLATFORM=offscreen "$FREECAD" "$here/instructions.py" >/dev/null 2>&1 || true
    cat "$here/out/.instructions.log" 2>/dev/null | tail -3
    python3 "$here/compose_instructions.py"
fi

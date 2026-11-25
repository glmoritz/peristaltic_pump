"""Entry point for freecadcmd: runs main() of the module named in $CAD_TARGET.

(freecadcmd does not set __name__ == "__main__", so the part scripts only
define main() and this runner calls it.)
"""
import importlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ok = importlib.import_module(os.environ["CAD_TARGET"]).main()
print(f"== {os.environ['CAD_TARGET']}: {'OK' if ok else 'CHECKS FAILED'} ==")

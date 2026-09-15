"""Standalone interpreter entry; keep protocol implementation shared with embed tests."""
import os
import runpy
import sys
from pathlib import Path

def main():
    if os.name == "posix":
        os.setpgid(0, 0)
    sys.path.insert(0, str(Path(__file__).parent))
    import structure_bridge
    sys.modules["_atom_studio_bridge"] = structure_bridge
    runpy.run_path(str(Path(__file__).with_name("interactive_worker.py")), run_name="__main__")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Integration checks against a relocated copy of the built Python payload."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bundle_macos_python import NativeBundle, run


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--check-executable", type=Path, required=True)
    parser.add_argument("--python-library", type=Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="atom relocated runtime ") as directory:
        app = Path(directory) / "Moved Atom Studio 原子.app"
        contents = app / "Contents"
        (contents / "MacOS").mkdir(parents=True)
        python_home = contents / "Resources/python"
        shutil.copytree(args.app / "Contents/Resources/python", python_home)
        shutil.copytree(args.app / "Contents/Frameworks/PythonRuntime", contents / "Frameworks/PythonRuntime")
        helper = contents / "MacOS/check"
        shutil.copy2(args.check_executable, helper)
        bundle = NativeBundle(app.resolve(), helper, args.python_library)
        bundle.rewrite_python_link(helper)
        # This checks loaded Mach-O images as well as Python import origins.
        bundle.validate(args.check_executable)

        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith(("PYTHON", "DYLD_")) and key != "__PYVENV_LAUNCHER__"}
        environment.update(PATH="/usr/bin:/bin")
        launcher = python_home / "bin/python3"
        environment_root = Path(directory) / "persistent environment"
        run(launcher, "-I", python_home / "atom_studio/environment_tool.py", "ensure", environment_root,
            cwd=directory, env=environment, timeout=30)
        moved = app.with_name("Moved again 原子.app")
        app.rename(moved)
        moved_home = moved / "Contents/Resources/python"
        run(moved_home / "bin/python3", "-I", moved_home / "atom_studio/environment_tool.py", "ensure", environment_root,
            cwd=directory, env=environment, timeout=30)
        run(environment_root / "bin/python", "-I", "-c",
            "import sys,ssl,os; from pathlib import Path; import ase,numpy,pip; "
            "assert 'Moved again' in sys.base_prefix; "
            "assert Path(os.environ['SSL_CERT_FILE']).is_file(); "
            "assert sys.prefix != sys.base_prefix", cwd=directory, env=environment, timeout=30)
        moved.rename(app)
        missing_home = contents / "Resources/python-hidden"
        python_home.rename(missing_home)
        run(helper, "--expect-missing-runtime", cwd=directory, env=environment, timeout=30)
        missing_home.rename(python_home)

        libdir = bundle.libdir
        mismatch = libdir.with_name("python0.0")
        libdir.rename(mismatch)
        run(helper, "--expect-missing-runtime", cwd=directory, env=environment, timeout=30)
        mismatch.rename(libdir)

        ase = libdir / "site-packages/ase"
        ase.rename(ase.with_name("ase-hidden"))
        run(helper, "--expect-missing-runtime", cwd=directory, env=environment, timeout=30)
        print("Relocation, missing runtime, incompatible runtime, and missing ASE checks passed")


if __name__ == "__main__":
    main()

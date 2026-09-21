"""Manage user-owned venvs. Invoked by the bundled launcher, never by GUI Python."""
import argparse
import contextlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import venv


def python_path(root):
    return root / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


@contextlib.contextmanager
def environment_lock(root, shared=False):
    root.mkdir(parents=True, exist_ok=True)
    with (root / ".atom-studio.lock").open("a+b") as stream:
        try:
            if os.name == "posix":
                import fcntl
                fcntl.flock(stream, (fcntl.LOCK_SH if shared else fcntl.LOCK_EX) | fcntl.LOCK_NB)
            else:
                import msvcrt
                stream.seek(0)
                if not stream.read(1):
                    stream.write(b"0"); stream.flush()
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as error:
            raise RuntimeError("This environment is busy in another Atom Studio process. Close its Python shell and retry.") from error
        yield


def ensure(root):
    marker = root / ".atom-studio-environment.json"
    expected = {"format": 2, "python": list(sys.version_info[:2]),
                "base": sys.base_prefix, "prompt": "Atom Studio " + root.name}
    current = json.loads(marker.read_text()) if marker.exists() else None
    if current and current["python"] != expected["python"]:
        raise RuntimeError("This environment requires a different Python version. Create a new environment.")
    if current != expected or not python_path(root).exists():
        print("Preparing Python environment…", flush=True)
        # Bundled ASE/pip remain read-only. Pip installs overrides into this venv.
        # Re-running after an app move repairs launcher links without deleting packages.
        if os.name != "nt":
            for name in ("python", "python3", f"python{sys.version_info.major}.{sys.version_info.minor}"):
                link = root / "bin" / name
                if link.is_symlink():
                    link.unlink()
        venv.EnvBuilder(system_site_packages=True, symlinks=os.name != "nt",
                        with_pip=False, prompt=expected["prompt"]).create(root)
        if os.name == "nt":
            shutil.copy2(Path(sys.base_prefix) / "bin/python.exe", root / "Scripts/python.exe")
            for dll in (Path(sys.base_prefix) / "bin").glob("*.dll"):
                shutil.copy2(dll, root / "Scripts" / dll.name)
        # The pip module is inherited, so venv --without-pip does not create its
        # entry points. Supply them explicitly: plain `pip` must not fall through
        # to the user's system/Conda pip after activation.
        from pip._vendor.distlib.scripts import ScriptMaker, enquote_executable
        maker = ScriptMaker(None, str(python_path(root).parent))
        maker.executable = enquote_executable(str(python_path(root)))
        maker.variants = {""}
        maker.clobber = True
        maker.set_mode = True
        for name in ("pip", "pip3", f"pip{sys.version_info.major}.{sys.version_info.minor}"):
            maker.make(f"{name} = pip._internal.cli.main:main")
        marker.write_text(json.dumps(expected))
    subprocess.run([str(python_path(root)), "-I", "-c",
                    "import sys; assert sys.prefix != sys.base_prefix; import pip"], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["ensure", "list", "install", "uninstall", "check"])
    parser.add_argument("root", type=Path)
    parser.add_argument("requirement", nargs="?")
    args = parser.parse_args()
    root = args.root.resolve()
    with environment_lock(root, shared=args.action == "list"):
        ensure(root)
        python = str(python_path(root))
        pip = [python, "-I", "-m", "pip", "--disable-pip-version-check", "--no-input"]
        env = os.environ.copy()
        env["PIP_REQUIRE_VIRTUALENV"] = "true"
        if args.action in ("install", "uninstall"):
            if not args.requirement or args.requirement.startswith("-"):
                raise ValueError("A package name is required")
            command = ["install", "--upgrade", args.requirement] if args.action == "install" else ["uninstall", "-y", args.requirement]
            subprocess.run(pip + command, env=env, check=True)
            subprocess.run(pip + ["check"], env=env, check=True)
        elif args.action == "check":
            subprocess.run(pip + ["check"], env=env, check=True)
        packages = []
        if args.action != "ensure":
            packages = json.loads(subprocess.check_output(pip + ["list", "--format=json"], env=env, text=True))
            local = json.loads(subprocess.check_output(pip + ["list", "--local", "--format=json"], env=env, text=True))
            names = {p["name"].lower().replace("_", "-") for p in local}
            for package in packages:
                package["local"] = package["name"].lower().replace("_", "-") in names
        print("@ATOM_ENV@" + json.dumps({"path": str(root), "python": python, "packages": packages}), flush=True)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Bundle and relocate Python's complete Mach-O dependency graph on macOS.

Only copies in the application are modified. Source paths are used at build time
to resolve dependencies; runtime load commands use bundle-relative paths.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import sysconfig
import tempfile
from importlib import metadata
from pathlib import Path


def run(*args: object, **kwargs) -> str:
    result = subprocess.run([str(arg) for arg in args], text=True, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(f"Command failed: {args}\n{result.stdout}{result.stderr}")
    return result.stdout


def fingerprint(path: Path) -> list[int]:
    stat = path.stat()
    return [stat.st_size, stat.st_mtime_ns]


def system_library(name: str) -> bool:
    return name.startswith(("/usr/lib/", "/System/Library/"))


def within(path: Path, directory: Path) -> bool:
    return path == directory or directory in path.parents


def macho(path: Path) -> bool:
    if not path.is_file():
        return False
    with path.open("rb") as stream:
        return stream.read(4) in {
            bytes.fromhex(value) for value in
            ("feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca")
        }


def load_commands(path: Path) -> tuple[list[str], list[str], bool]:
    dependencies, rpaths = [], []
    command, has_id = "", False
    for line in run("/usr/bin/otool", "-l", path).splitlines():
        line = line.strip()
        if line.startswith("cmd "):
            command = line[4:]
            has_id |= command == "LC_ID_DYLIB"
        elif line.startswith("name ") and command in {
            "LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB", "LC_LOAD_UPWARD_DYLIB"
        }:
            dependencies.append(line[5:].rsplit(" (offset", 1)[0])
        elif line.startswith("path ") and command == "LC_RPATH":
            rpaths.append(line[5:].rsplit(" (offset", 1)[0])
    return list(dict.fromkeys(dependencies)), list(dict.fromkeys(rpaths)), has_id


def relative_load_path(loader: Path, dependency: Path) -> str:
    # macOS exposes the same temporary directories through /var and /private/var.
    # Normalize both paths before computing a relocatable load command.
    return "@loader_path/" + os.path.relpath(dependency.resolve(), loader.resolve().parent)


class NativeBundle:
    def __init__(self, app: Path, executable: Path, library: Path, launcher: Path | None = None):
        app = app.resolve()
        self.app = app
        self.executable = executable.resolve()
        self.library = library.resolve()
        self.launcher = launcher.resolve() if launcher else None
        self.python_home = app / "Contents/Resources/python"
        self.libdir = self.python_home / f"lib/python{sys.version_info.major}.{sys.version_info.minor}"
        self.native_dir = app / "Contents/Frameworks/PythonRuntime"
        self.manifest = self.python_home / ".atom-studio-native-manifest.json"
        self.destinations: dict[Path, Path] = {}
        self.sources: dict[Path, Path] = {}
        self.architectures = set(run("/usr/bin/lipo", "-archs", executable).split())
        self.search_dirs = [self.library.parent, Path(sys.base_prefix) / "lib", Path(sys.prefix) / "lib"]

    def configuration(self) -> dict:
        package_manifest = self.python_home / ".atom-studio-package-manifest.json"
        stdlib_manifest = self.python_home / ".atom-studio-stdlib-manifest.cmake"
        return {
            "script": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "library": str(self.library),
            "architectures": sorted(self.architectures),
            "packages": package_manifest.read_text(),
            "stdlib": stdlib_manifest.read_text(),
            "launcher": str(self.launcher),
        }

    def up_to_date(self) -> bool:
        try:
            previous = json.loads(self.manifest.read_text())
            if previous["configuration"] != self.configuration():
                return False
            return all(fingerprint(Path(path)) == stamp for path, stamp in previous["files"].items())
        except (OSError, ValueError, KeyError):
            return False

    def register(self, source: Path, destination: Path) -> Path:
        source = source.resolve()
        existing = self.destinations.get(source)
        if existing is not None:
            return existing
        if destination in self.sources and self.sources[destination] != source:
            raise RuntimeError(f"Conflicting native libraries at {destination}: {source}, {self.sources[destination]}")
        self.destinations[source] = destination
        self.sources[destination] = source
        return destination

    def collect_python_extensions(self) -> None:
        package_manifest = json.loads((self.python_home / ".atom-studio-package-manifest.json").read_text())
        package_roots = {}
        for distribution in package_manifest["distributions"].values():
            installed = metadata.distribution(distribution["name"])
            for root in distribution["roots"]:
                package_roots[root] = Path(installed.locate_file(root))
        stdlib = Path(sysconfig.get_path("stdlib"))
        for destination in sorted(self.libdir.rglob("*")):
            if destination.is_symlink():
                if not destination.exists() or not within(destination.resolve(), self.app):
                    raise RuntimeError(f"Python bundle contains a broken or external symlink: {destination}")
                continue
            if not macho(destination):
                continue
            relative = destination.relative_to(self.libdir)
            if relative.parts[0] == "site-packages":
                source = package_roots[relative.parts[1]].joinpath(*relative.parts[2:])
            else:
                source = stdlib / relative
            self.register(source, destination)

    def resolve(self, name: str, source: Path, rpaths: list[str]) -> Path:
        def expand(value: str) -> str:
            return value.replace("@loader_path", str(source.parent)).replace(
                "@executable_path", str(Path(sys.executable).resolve().parent))

        if name.startswith("@rpath/"):
            suffix = name[len("@rpath/"):]
            candidates = [Path(expand(path)) / suffix for path in rpaths]
            candidates.extend(path / suffix for path in self.search_dirs)
        else:
            candidates = [Path(expand(name))]
        for candidate in candidates:
            # System libraries may exist only in the dyld shared cache.
            if system_library(str(candidate)) or candidate.is_file():
                return candidate.resolve()
        raise RuntimeError(f"Cannot resolve {name} required by {source}; searched {candidates}")

    def rewrite_python_link(self, executable: Path) -> None:
        dependencies, rpaths, _ = load_commands(executable)
        python_links = [name for name in dependencies if Path(name).name == self.library.name]
        if len(python_links) != 1:
            raise RuntimeError(f"Expected one Python shared library in {executable}: {python_links}")
        destination = self.native_dir / self.library.name
        replacement = relative_load_path(executable, destination)
        changes = []
        if python_links[0] != replacement:
            changes.extend(["-change", python_links[0], replacement])
        # Retain Qt development paths, but never a path into the source Python.
        for rpath in rpaths:
            if any(within(Path(rpath), prefix) for prefix in {Path(sys.prefix), Path(sys.base_prefix), self.library.parent}):
                changes.extend(["-delete_rpath", rpath])
        if changes:
            run("/usr/bin/install_name_tool", *changes, executable)
            run("/usr/bin/codesign", "--force", "--sign", "-", executable)

    def synchronize(self) -> None:
        if self.up_to_date():
            print("Bundled Python native libraries are up to date")
            return

        self.collect_python_extensions()
        if self.launcher:
            self.register(self.launcher, self.python_home / "bin/python3")
        self.register(self.library, self.native_dir / self.library.name)
        pending = list(self.destinations)
        processed = set()
        while pending:
            source = pending.pop()
            if source in processed:
                continue
            processed.add(source)
            destination = self.destinations[source]
            available = set(run("/usr/bin/lipo", "-archs", source).split())
            if not self.architectures <= available:
                raise RuntimeError(f"{source} has architectures {available}; app requires {self.architectures}")
            dependencies, rpaths, has_id = load_commands(source)
            changes = []
            for name in dependencies:
                if system_library(name):
                    continue
                resolved = self.resolve(name, source, rpaths)
                if system_library(str(resolved)):
                    replacement = str(resolved)
                else:
                    target = self.register(resolved, self.native_dir / resolved.name)
                    pending.append(resolved)
                    replacement = relative_load_path(destination, target)
                if name != replacement:
                    changes.extend(["-change", name, replacement])
            # All non-system dependencies now have explicit bundle-relative paths.
            for rpath in rpaths:
                changes.extend(["-delete_rpath", rpath])
            if has_id:
                changes.extend(["-id", "@rpath/" + destination.name])
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            destination.chmod(destination.stat().st_mode | 0o200)
            if changes:
                run("/usr/bin/install_name_tool", *changes, destination)
            run("/usr/bin/codesign", "--force", "--sign", "-", destination)

        # Remove only obsolete files in the directory owned by this bundler.
        for path in self.native_dir.iterdir():
            if path.is_file() and path not in self.sources:
                path.unlink()
        self.rewrite_python_link(self.executable)
        self.audit()
        files = set(self.destinations) | set(self.sources) | {self.executable}
        self.manifest.write_text(json.dumps({
            "configuration": self.configuration(),
            "files": {str(path): fingerprint(path) for path in sorted(files)},
        }, indent=2))
        print(f"Bundled and relocated {len(processed)} Python native binaries")

    def audit(self) -> None:
        for destination in self.sources:
            dependencies, rpaths, _ = load_commands(destination)
            if rpaths:
                raise RuntimeError(f"Unexpected Python RPATHs: {destination}: {rpaths}")
            for name in dependencies:
                if system_library(name):
                    continue
                if not name.startswith("@loader_path/"):
                    raise RuntimeError(f"External dependency in {destination}: {name}")
                resolved = (destination.parent / name[len("@loader_path/"):]).resolve()
                if not within(resolved, self.app) or not resolved.is_file():
                    raise RuntimeError(f"Dependency escapes the bundle or is missing: {destination}: {name}")

    def validate(self, check_executable: Path) -> None:
        # Use the actual embedded runtime without Qt or a GUI. The temporary
        # helper is never part of the distributed application.
        helper = self.app / "Contents/MacOS/.atom-python-runtime-check"
        try:
            shutil.copy2(check_executable, helper)
            self.rewrite_python_link(helper)
            with tempfile.TemporaryDirectory(prefix="atom-python-isolation-") as directory:
                root = Path(directory)
                for name in ("numpy.py", "sitecustomize.py", "usercustomize.py"):
                    (root / name).write_text("raise RuntimeError('External Python code was imported')\n")
                environment = {key: value for key, value in os.environ.items()
                               if not key.startswith(("PYTHON", "DYLD_")) and key != "__PYVENV_LAUNCHER__"}
                environment.update(PYTHONHOME=str(root / "missing-python"), PYTHONPATH=str(root),
                                   PYTHONUSERBASE=str(root), PYTHONSTARTUP=str(root / "sitecustomize.py"),
                                   PATH="/usr/bin:/bin", ATOM_EXPECTED_PYTHON_HOME=str(self.python_home))
                print(run(helper, cwd=root, env=environment, timeout=60).strip())
        finally:
            helper.unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--python-library", required=True, type=Path)
    parser.add_argument("--check-executable", required=True, type=Path)
    parser.add_argument("--launcher", type=Path)
    args = parser.parse_args()
    bundle = NativeBundle(args.app.resolve(), args.executable.resolve(), args.python_library, args.launcher)
    bundle.synchronize()
    bundle.validate(args.check_executable)


if __name__ == "__main__":
    main()

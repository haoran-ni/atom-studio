#!/usr/bin/env python3
"""Stage, audit, locally sign and package an existing macOS Release build."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import plistlib
import re
import subprocess
import sys
import tempfile

# -I excludes the script directory, so add only our own helpers explicitly.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bundle_macos_python import load_commands, macho, run, system_library, within


def version_tuple(value: str) -> tuple[int, int, int]:
    parts = [int(part) for part in value.split(".")]
    return tuple((parts + [0, 0, 0])[:3])


def deployment_versions(commands: str) -> list[str]:
    versions = []
    command = ""
    for line in commands.splitlines():
        fields = line.split()
        if len(fields) == 2 and fields[0] == "cmd":
            command = fields[1]
        elif len(fields) == 2 and ((command == "LC_BUILD_VERSION" and fields[0] == "minos") or
                                  (command == "LC_VERSION_MIN_MACOSX" and fields[0] == "version")):
            versions.append(fields[1])
    return versions


def expand_path(value: str, loader: Path, executable: Path) -> Path:
    return Path(value.replace("@loader_path", str(loader.parent)).replace(
        "@executable_path", str(executable.parent))).resolve()


def native_files(app: Path) -> list[Path]:
    binaries = []
    for path in sorted(app.rglob("*")):
        if path.is_symlink():
            if not within(path.resolve(), app) or not path.exists():
                raise RuntimeError(f"Broken or external symlink in app: {path}")
        elif macho(path):
            binaries.append(path)
    return binaries


def resolve_dependency(name: str, loader: Path, executable: Path,
                       rpaths: list[str], executable_rpaths: list[str], app: Path) -> Path | None:
    if system_library(name):
        return None  # These may exist only in Apple's dyld shared cache.
    if name.startswith("@rpath/"):
        roots = [expand_path(path, loader, executable) for path in rpaths]
        roots += [expand_path(path, executable, executable) for path in executable_rpaths]
        candidates = [root / name[len("@rpath/"):] for root in roots]
    elif name.startswith(("@loader_path/", "@executable_path/")):
        candidates = [expand_path(name, loader, executable)]
    else:
        raise RuntimeError(f"Non-relocatable dependency in {loader}: {name}")
    for candidate in candidates:
        resolved = candidate.resolve()
        if within(resolved, app) and resolved.is_file():
            return resolved
    raise RuntimeError(f"Missing bundled dependency in {loader}: {name}")


def audit_and_relocate(app: Path, executable: Path, architecture: str, minimum: str) -> list[Path]:
    binaries = native_files(app)
    if executable not in binaries:
        raise RuntimeError(f"Missing Mach-O application executable: {executable}")
    # macdeployqt can leave build-time RPATHs even when it copied every library.
    # Remove them only from the staged copy, then require all loads to resolve.
    for binary in binaries:
        _, rpaths, _ = load_commands(binary)
        for rpath in rpaths:
            if not within(expand_path(rpath, binary, executable), app):
                run("/usr/bin/install_name_tool", "-delete_rpath", rpath, binary)
    _, executable_rpaths, _ = load_commands(executable)
    for binary in binaries:
        architectures = run("/usr/bin/lipo", "-archs", binary).split()
        if architecture not in architectures:
            raise RuntimeError(f"{binary} does not contain {architecture}: {architectures}")
        commands = run("/usr/bin/otool", "-arch", architecture, "-l", binary)
        for required in deployment_versions(commands):
            if version_tuple(required) > version_tuple(minimum):
                raise RuntimeError(f"{binary} requires macOS {required}, newer than this build's {minimum}")
        dependencies, rpaths, _ = load_commands(binary)
        for name in dependencies:
            resolve_dependency(name, binary, executable, rpaths, executable_rpaths, app)
    print(f"Verified architecture, minimum macOS and bundled dependencies for {len(binaries)} native files", flush=True)
    return binaries


def sign_app(app: Path, binaries: list[Path]) -> None:
    # Sign individual Python extensions too: they live under Resources and
    # codesign --deep does not reliably discover them. Seal outer bundles last.
    info = plistlib.loads((app / "Contents/Info.plist").read_bytes())
    executable = app / "Contents/MacOS" / info["CFBundleExecutable"]
    # codesign treats a bundle's main executable as the entire bundle. Signing
    # it here would try to seal still-unsigned plugins; sign the app last below.
    for binary in sorted((path for path in binaries if path != executable),
                         key=lambda path: len(path.parts), reverse=True):
        run("/usr/bin/codesign", "--force", "--sign", "-", "--timestamp=none", binary)
    bundles = [path for path in app.rglob("*") if path.is_dir() and not path.is_symlink()
               and path.suffix in {".framework", ".app", ".xpc", ".bundle", ".plugin"}]
    for bundle in sorted(bundles, key=lambda path: len(path.parts), reverse=True) + [app]:
        run("/usr/bin/codesign", "--force", "--sign", "-", "--timestamp=none", bundle)
    for binary in binaries:
        run("/usr/bin/codesign", "--verify", "--strict", binary)
    run("/usr/bin/codesign", "--verify", "--deep", "--strict", app)


def smoke_test(executable: Path, directory: Path) -> None:
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("PYTHON", "DYLD_", "QT_", "QML"))
                   and key not in {"__PYVENV_LAUNCHER__", "VIRTUAL_ENV"}}
    environment.update(PATH="/usr/bin:/bin:/usr/sbin:/sbin", QML_DISABLE_DISK_CACHE="1")
    print("Opening the packaged app briefly to verify Python, Qt and QML startup...", flush=True)
    process = subprocess.Popen([str(executable)], cwd=directory, env=environment,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        try:
            output, _ = process.communicate(timeout=10)
            raise RuntimeError(f"Packaged app exited during startup ({process.returncode}):\n{output}")
        except subprocess.TimeoutExpired:
            process.terminate()
            output, _ = process.communicate(timeout=10)
        if "ATOM-STUDIO initialized successfully" not in output:
            raise RuntimeError(f"Packaged app did not finish QML initialization:\n{output}")
        print(output.strip(), flush=True)
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()


def package(args: argparse.Namespace) -> Path:
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".package-", dir=output_dir) as directory:
        temporary = Path(directory)
        stage = temporary / "disk"
        stage.mkdir()
        app = stage / "ATOM-STUDIO.app"
        run("/usr/bin/ditto", "--noextattr", "--noqtn", "--norsrc", args.app.resolve(), app)
        info_path = app / "Contents/Info.plist"
        info = plistlib.loads(info_path.read_bytes())
        version = info["CFBundleShortVersionString"]
        if not re.fullmatch(r"[0-9A-Za-z.+_-]+", version):
            raise RuntimeError(f"Invalid app version for disk-image filename: {version!r}")
        executable = app / "Contents/MacOS" / info["CFBundleExecutable"]
        # Homebrew merges unrelated Qt plugins under its shared prefix. Stage
        # only plugins from our selected Qt modules, then let macdeployqt inspect
        # these binaries instead of deploying every globally installed plugin.
        for plugins in args.plugin_dir:
            if not plugins.is_dir():
                raise RuntimeError(f"Missing Qt plugin directory: {plugins}")
            # Like macdeployqt, copy only dynamic plugins. Static archives and
            # .prl build metadata cannot be sealed as code under Contents/PlugIns.
            for plugin in plugins.rglob("*.dylib"):
                destination = app / "Contents/PlugIns" / plugin.relative_to(plugins)
                destination.parent.mkdir(parents=True, exist_ok=True)
                run("/usr/bin/ditto", "--noextattr", "--noqtn", "--norsrc", plugin.resolve(), destination)
        # Qt's rpath resolver needs this prefix after plugins have been copied
        # away from their formula directories. The final audit removes it.
        _, initial_rpaths, _ = load_commands(executable)
        library_path = str(args.library_dir.resolve())
        if library_path not in initial_rpaths:
            run("/usr/bin/install_name_tool", "-add_rpath", library_path, executable)
        # Python extensions were already relocated and validated by bundle-python.
        # Keep them out of macdeployqt's all-pairs install-name rewrite pass, which
        # otherwise launches tens of thousands of redundant subprocesses. Keep
        # PythonRuntime in Frameworks so the main executable's library resolves.
        python_home = app / "Contents/Resources/python"
        parked_python = temporary / "python"
        python_home.rename(parked_python)
        try:
            deployment = subprocess.run([str(args.macdeployqt.resolve()), str(app),
                                         f"-qmldir={args.qml_dir.resolve()}",
                                         f"-qmlimport={args.qml_import_dir.resolve()}",
                                         f"-libpath={library_path}", "-no-plugins", "-verbose=2"],
                                        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900)
        finally:
            parked_python.rename(python_home)
        deployment_log = output_dir / "qt-deploy.log"
        deployment_log.write_text(deployment.stdout, encoding="utf-8")
        print(f"Qt deployment log: {deployment_log}", flush=True)
        # Some macdeployqt versions return zero even after a missing framework.
        if deployment.returncode or re.search(r"\bERROR\b", deployment.stdout, re.IGNORECASE):
            print(deployment.stdout, flush=True)
            raise RuntimeError("Qt deployment failed; see macdeployqt output above. No disk image was replaced.")
        (app / "Contents/Resources/qt.conf").write_text(
            "[Paths]\nPlugins = PlugIns\nImports = Resources/qml\nQmlImports = Resources/qml\n")
        info["LSMinimumSystemVersion"] = args.minimum_macos
        info_path.write_bytes(plistlib.dumps(info))
        binaries = audit_and_relocate(app, executable, args.architecture, args.minimum_macos)
        sign_app(app, binaries)
        smoke_test(executable, temporary)
        run("/usr/bin/codesign", "--verify", "--deep", "--strict", app)
        (stage / "Applications").symlink_to("/Applications", target_is_directory=True)
        (stage / "INSTALL.txt").write_text(
            f"ATOM-STUDIO {version}\n\n"
            "Drag ATOM-STUDIO.app onto Applications, then eject this disk image.\n"
            "Open ATOM-STUDIO from Applications. Python and Qt are included.\n\n"
            f"Built for {args.architecture}, macOS {args.minimum_macos} or newer.\n"
            "This build uses free local signatures and is not notarized by Apple.\n"
            "If macOS blocks a downloaded copy, attempt to open it, then use\n"
            "System Settings > Privacy & Security > Open Anyway, if you trust it.\n",
            encoding="utf-8")
        name = f"ATOM-STUDIO-{version}-macOS-{args.architecture}.dmg"
        image = temporary / name
        print("Creating and verifying compressed disk image...", flush=True)
        run("/usr/bin/hdiutil", "create", "-volname", "ATOM-STUDIO", "-srcfolder", stage,
            "-fs", "HFS+", "-format", "UDZO", image)
        run("/usr/bin/hdiutil", "verify", image)
        # Preserve a previous successful image until every packaging check passes.
        destination = output_dir / name
        with image.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        image.replace(destination)
        (output_dir / f"{name}.sha256").write_text(f"{digest}  {name}\n")
        (output_dir / "latest-dmg.txt").write_text(str(destination) + "\n")
        return destination


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--macdeployqt", required=True, type=Path)
    parser.add_argument("--qml-dir", required=True, type=Path)
    parser.add_argument("--qml-import-dir", required=True, type=Path)
    parser.add_argument("--library-dir", required=True, type=Path)
    parser.add_argument("--plugin-dir", action="append", default=[], type=Path)
    parser.add_argument("--architecture", required=True, choices=("arm64", "x86_64"))
    parser.add_argument("--minimum-macos", required=True)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("Packaging requires macOS")
    try:
        version_tuple(args.minimum_macos)
        print(f"Disk image ready: {package(args)}", flush=True)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")


if __name__ == "__main__":
    main()

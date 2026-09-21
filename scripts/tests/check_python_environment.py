"""Offline integration: real pip, console entry points, worker imports and repair."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import zipfile


def run(*args, **kwargs):
    result = subprocess.run([str(a) for a in args], text=True, capture_output=True, timeout=60, **kwargs)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return result.stdout


def wheel(directory):
    files = {
        "atom_env_fixture.py": "VALUE = 42\ndef main():\n    print('entry-point-works')\n",
        "atom_env_fixture-1.0.dist-info/METADATA": "Metadata-Version: 2.1\nName: atom-env-fixture\nVersion: 1.0\n",
        "atom_env_fixture-1.0.dist-info/WHEEL": "Wheel-Version: 1.0\nGenerator: Atom Studio test\nRoot-Is-Purelib: true\nTag: py3-none-any\n",
        "atom_env_fixture-1.0.dist-info/entry_points.txt": "[console_scripts]\natom-env-fixture = atom_env_fixture:main\n",
    }
    records = []
    for name, content in files.items():
        data = content.encode()
        digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode()
        records.append(f"{name},sha256={digest},{len(data)}")
    records.append("atom_env_fixture-1.0.dist-info/RECORD,,")
    files["atom_env_fixture-1.0.dist-info/RECORD"] = "\n".join(records) + "\n"
    path = directory / "atom_env_fixture-1.0-py3-none-any.whl"
    with zipfile.ZipFile(path, "w") as archive:
        for name, content in files.items():
            archive.writestr(name, content)
    return path


def source_package(directory):
    # A tiny PEP 517 backend must be installed into pip's isolated build env.
    # This catches launchers that accidentally disable pip's PYTHONPATH setup.
    backend = directory / "atom_build_backend-1.0-py3-none-any.whl"
    with zipfile.ZipFile(backend, "w") as archive:
        archive.writestr("atom_build_backend.py", "import shutil\nfrom pathlib import Path\ndef build_wheel(wheel_directory, config_settings=None, metadata_directory=None):\n    source = next(Path.cwd().glob('*.whl'))\n    shutil.copy2(source, Path(wheel_directory) / source.name)\n    return source.name\n")
        archive.writestr("atom_build_backend-1.0.dist-info/METADATA", "Metadata-Version: 2.1\nName: atom-build-backend\nVersion: 1.0\n")
        archive.writestr("atom_build_backend-1.0.dist-info/WHEEL", "Wheel-Version: 1.0\nRoot-Is-Purelib: true\nTag: py3-none-any\n")
        archive.writestr("atom_build_backend-1.0.dist-info/RECORD", "")
    source = directory / "source"
    source.mkdir()
    shutil.copy2(wheel(directory), source)
    (source / "pyproject.toml").write_text('[build-system]\nrequires = ["atom-build-backend==1.0"]\nbuild-backend = "atom_build_backend"\n')
    return source


def worker(python, entry, environment):
    process = subprocess.Popen([str(python), "-I", "-u", str(entry)], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=environment)
    lines = queue.Queue()
    threading.Thread(target=lambda: [lines.put(line) for line in process.stdout], daemon=True).start()
    def receive(kind):
        while True:
            line = lines.get(timeout=20)
            if line.startswith("@ATOM_SHELL@"):
                message = json.loads(line[len("@ATOM_SHELL@"):])
                if message["type"] == kind:
                    return message
    def send(message):
        process.stdin.write(json.dumps(message) + "\n"); process.stdin.flush()
    try:
        receive("ready")
        send({"type": "sync", "documents": [], "ids": []})
        code = "import atom_env_fixture, os, sys, subprocess\nassert atom_env_fixture.VALUE == 42\nassert sys.prefix != sys.base_prefix\nassert os.environ['HF_HOME'].endswith('fake HF cache')\nsubprocess.run([sys.executable, '-c', 'import atom_env_fixture; assert atom_env_fixture.VALUE == 42'], check=True)"
        send({"type": "run", "run": 1, "code": code, "directory": str(python.parent), "fps": 10})
        assert receive("done")["success"], "Installed package unavailable to shell"
    finally:
        process.stdin.close(); process.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--manager", type=Path)
    parser.add_argument("--app", type=Path)
    args = parser.parse_args()
    bundle = args.bundle.resolve()
    launcher = bundle / "bin/python3"
    helper = bundle / "atom_studio/environment_tool.py"
    entry = bundle / "atom_studio/worker_entry.py"
    with tempfile.TemporaryDirectory(prefix="atom env ' 原子 ") as temporary:
        directory = Path(temporary)
        root = directory / "environment"
        environment = os.environ.copy()
        environment.update(PYTHONHOME="/does-not-exist", PYTHONPATH=str(directory),
                           HF_HOME=str(directory / "fake HF cache"), PIP_NO_INDEX="1")
        if args.manager:
            wheel(directory)
            print(run(args.manager, args.app, directory / "manager", directory, "-platform", "offscreen", env=environment))
            return
        (directory / "sitecustomize.py").write_text("raise RuntimeError('external site imported')")
        run(launcher, "-I", helper, "ensure", root, env=environment)
        python = root / "bin/python"
        terminal_environment = {k: v for k, v in environment.items() if not k.startswith("PYTHON")}
        run(root / "bin/pip", "install", "--no-index", "--no-deps", wheel(directory), env=terminal_environment)
        assert "entry-point-works" in run(root / "bin/atom-env-fixture", env=terminal_environment)
        worker(python, entry, environment)
        worker(python, entry, environment)  # Survives a worker restart.
        # An environment created before display-name tracking must refresh its
        # activation prompt without losing packages the user installed.
        marker_path = root / ".atom-studio-environment.json"
        marker = json.loads(marker_path.read_text())
        marker.pop("prompt")
        marker_path.write_text(json.dumps(marker))
        activation = root / "bin/activate"
        activation.write_text(activation.read_text().replace("Atom Studio environment", "Old name environment"))
        run(launcher, "-I", helper, "ensure", root, env=environment)
        assert "Atom Studio environment" in activation.read_text()
        assert "Old name environment" not in activation.read_text()
        run(python, "-I", "-c", "import atom_env_fixture; assert atom_env_fixture.VALUE == 42", env=environment)
        other = directory / "other"
        run(launcher, "-I", helper, "ensure", other, env=environment)
        run(other / "bin/python", "-I", "-c", "import importlib.util; assert importlib.util.find_spec('atom_env_fixture') is None", env=environment)
        run(launcher, "-I", "-c", "import importlib.util; assert importlib.util.find_spec('atom_env_fixture') is None", env=environment)
        # Broken launcher links are repaired without removing installed packages.
        (root / "bin/python3").unlink()
        (root / "bin/python3").symlink_to("/does-not-exist")
        run(launcher, "-I", helper, "ensure", root, env=environment)
        run(python, "-I", "-c", "import atom_env_fixture; assert atom_env_fixture.VALUE == 42", env=environment)
        output = run(launcher, "-I", helper, "uninstall", root, "atom-env-fixture", env=environment)
        assert '"name": "atom-env-fixture"' not in output
        run(python, "-I", "-c", "import importlib.util; assert importlib.util.find_spec('atom_env_fixture') is None", env=environment)
        run(python, "-I", "-m", "pip", "install", "--no-index", "--find-links", directory,
            source_package(directory), env=environment)
        worker(python, entry, terminal_environment)
    print("PASS: venv, offline pip install/uninstall, console entry point, worker import/restart, prompt upgrade, isolation, launcher repair, credential paths")


if __name__ == "__main__":
    main()

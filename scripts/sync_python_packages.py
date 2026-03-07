#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
from importlib import metadata
from pathlib import Path, PurePosixPath

try:
    from packaging.markers import default_environment
    from packaging.requirements import Requirement
except ImportError:  # pragma: no cover - packaging is expected to be available
    Requirement = None
    default_environment = None


def canonicalize_name(name: str) -> str:
    return re.sub(r"[-_.]+", "-", name).lower()


def parse_requirement(line: str):
    if Requirement is None:
        match = re.match(r"[A-Za-z0-9_.-]+", line)
        if match is None:
            raise ValueError(f"Unsupported requirement line: {line!r}")
        return match.group(0)
    return Requirement(line)


def requirement_name(requirement) -> str:
    return requirement if isinstance(requirement, str) else requirement.name


def requirement_enabled(requirement) -> bool:
    if isinstance(requirement, str):
        return True
    if requirement.marker is None:
        return True
    environment = default_environment()
    environment["extra"] = ""
    return requirement.marker.evaluate(environment)


def load_root_requirements(requirements_file: Path):
    requirements = []
    for raw_line in requirements_file.read_text().splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        requirements.append(parse_requirement(line))
    return requirements


def distribution_roots(distribution: metadata.Distribution) -> list[str]:
    roots: set[str] = set()
    files = distribution.files or []
    for file_entry in files:
        path = PurePosixPath(str(file_entry))
        if not path.parts or path.parts[0] == "..":
            continue
        roots.add(path.parts[0])

    if roots:
        return sorted(roots)

    top_level = distribution.read_text("top_level.txt")
    if top_level:
        for line in top_level.splitlines():
            entry = line.strip()
            if entry:
                roots.add(entry)

    dist_info_path = getattr(distribution, "_path", None)
    if dist_info_path is not None:
        roots.add(Path(dist_info_path).name)
    return sorted(roots)


def resolve_distributions(requirements) -> dict[str, dict[str, object]]:
    resolved: dict[str, dict[str, object]] = {}
    queue = [requirement_name(req) for req in requirements if requirement_enabled(req)]

    while queue:
        requested_name = queue.pop(0)
        distribution = metadata.distribution(requested_name)
        distribution_name = distribution.metadata.get("Name", requested_name)
        key = canonicalize_name(distribution_name)
        if key in resolved:
            continue

        resolved[key] = {
            "name": distribution_name,
            "version": distribution.version,
            "roots": distribution_roots(distribution),
        }

        for dependency_line in distribution.requires or []:
            dependency = parse_requirement(dependency_line)
            if requirement_enabled(dependency):
                queue.append(requirement_name(dependency))

    return dict(sorted(resolved.items()))


def build_manifest(requirements_file: Path, distributions: dict[str, dict[str, object]]) -> dict[str, object]:
    return {
        "python_executable": sys.executable,
        "python_version": sys.version.split()[0],
        "requirements_file": str(requirements_file.resolve()),
        "distributions": distributions,
    }


def manifest_matches(existing: dict[str, object], current: dict[str, object], target_site_packages: Path) -> bool:
    if existing != current:
        return False

    for distribution_info in current["distributions"].values():
        for root in distribution_info["roots"]:
            if not (target_site_packages / root).exists():
                return False

    return True


def copy_root(source: Path, destination: Path) -> None:
    if source.is_dir():
        shutil.copytree(source, destination, dirs_exist_ok=True)
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def sync_site_packages(target_site_packages: Path, distributions: dict[str, dict[str, object]]) -> None:
    if target_site_packages.exists():
        shutil.rmtree(target_site_packages)
    target_site_packages.mkdir(parents=True, exist_ok=True)

    copied_roots: set[str] = set()
    for distribution_info in distributions.values():
        distribution = metadata.distribution(distribution_info["name"])
        for root in distribution_info["roots"]:
            if root in copied_roots:
                continue
            source = Path(distribution.locate_file(root))
            if not source.exists():
                raise FileNotFoundError(f"Cannot bundle missing Python package path: {source}")
            copy_root(source, target_site_packages / root)
            copied_roots.add(root)


def main() -> int:
    parser = argparse.ArgumentParser(description="Synchronize Python packages into a bundle.")
    parser.add_argument("--requirements-file", required=True)
    parser.add_argument("--target-site-packages", required=True)
    parser.add_argument("--manifest-file", required=True)
    args = parser.parse_args()

    requirements_file = Path(args.requirements_file)
    target_site_packages = Path(args.target_site_packages)
    manifest_file = Path(args.manifest_file)

    requirements = load_root_requirements(requirements_file)
    distributions = resolve_distributions(requirements)
    current_manifest = build_manifest(requirements_file, distributions)

    existing_manifest = None
    if manifest_file.exists():
        try:
            existing_manifest = json.loads(manifest_file.read_text())
        except json.JSONDecodeError:
            existing_manifest = None

    if existing_manifest and manifest_matches(existing_manifest, current_manifest, target_site_packages):
        print("Bundled Python packages are up to date")
        return 0

    sync_site_packages(target_site_packages, distributions)

    manifest_file.parent.mkdir(parents=True, exist_ok=True)
    manifest_file.write_text(json.dumps(current_manifest, indent=2, sort_keys=True))

    copied = ", ".join(
        f"{info['name']}=={info['version']}" for info in current_manifest["distributions"].values()
    )
    print(f"Synchronized bundled Python packages: {copied}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Validate exported geometry with ASE, independently of the native writers."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile

import numpy as np
from ase.io import read
from ase.io.cif import parse_cif


def check_fixture(fixture):
    path = Path(fixture["path"])
    fmt = fixture["format"]
    ase_format = {".in": "aims", ".cif": "cif", "POSCAR": "vasp", ".xyz": "extxyz"}[fmt]
    atoms = read(path, format=ase_format)
    positions = np.asarray(fixture["positions"])
    numbers = np.asarray(fixture["numbers"])
    cell = np.asarray(fixture["cell"])
    has_cell = fixture["has_lattice"]
    text = path.read_text(encoding="utf-8")

    assert len(atoms) == len(numbers), (path, "atom count")
    if fixture["scenario"] == "edited":
        assert len(atoms) == 14  # 4 atoms * 2 * 1 * 2, then two deleted atoms.
    if fmt == "POSCAR":
        # POSCAR groups species while preserving the positions for each atom.
        species = list(dict.fromkeys(numbers.tolist()))
        indices = np.concatenate([np.flatnonzero(numbers == z) for z in species])
        np.testing.assert_array_equal(atoms.numbers, numbers[indices])
        np.testing.assert_allclose(atoms.positions, positions[indices], atol=1e-12, rtol=0)
    elif fmt == ".cif":
        # CIF uses cell lengths/angles. Check the raw fractional sites too:
        # readers may rotate the cell to conventional axes and wrap positions.
        block = next(parse_cif(str(path)))
        kind = "fract" if has_cell else "cartn"
        sites = np.array([block[f"_atom_site_{kind}_{d}"] for d in "xyz"]).T
        expected_sites = positions @ np.linalg.inv(cell) if has_cell else positions
        np.testing.assert_allclose(sites, expected_sites, atol=1e-12, rtol=0)
        np.testing.assert_array_equal(atoms.numbers, numbers)
        if has_cell:
            np.testing.assert_allclose(atoms.cell.array @ atoms.cell.array.T, cell @ cell.T, atol=1e-10, rtol=0)
            assert block.get("_space_group_it_number") == 1
        else:
            np.testing.assert_allclose(atoms.positions, positions, atol=1e-12, rtol=0)
    else:
        np.testing.assert_array_equal(atoms.numbers, numbers)
        np.testing.assert_allclose(atoms.positions, positions, atol=1e-12, rtol=0)

    if has_cell:
        if fmt != ".cif":
            np.testing.assert_allclose(atoms.cell.array, cell, atol=1e-12, rtol=0)
    else:
        assert not atoms.cell.any(), (path, "unexpected lattice")
        assert "lattice_vector" not in text and "Lattice=" not in text and "_cell_length_" not in text
    if fmt == ".xyz":
        assert "Properties=species:S:1:pos:R:3" in text
        np.testing.assert_array_equal(atoms.pbc, fixture["pbc"] if has_cell else [False] * 3)
    print(f"PASS {fixture['scenario']} {fmt}: {len(atoms)} atoms, lattice={has_cell}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exporter", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="atom-structure-export-") as temporary:
        output = Path(temporary) / "geometry exports å 测试"
        subprocess.run([args.exporter, str(output)], check=True, timeout=60)
        for fixture in json.loads((output / "manifest.json").read_text(encoding="utf-8")):
            check_fixture(fixture)


if __name__ == "__main__":
    main()

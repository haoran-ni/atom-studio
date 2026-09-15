"""Calculation-free ASE snapshots shared by file I/O and the interactive process."""
import copy
import numpy as np
from ase import Atoms
from ase.io.jsonio import encode, decode

ID_ARRAY = "_atom_studio_ids"
_next_identity = 1


def pack(atoms):
    global _next_identity
    if not isinstance(atoms, Atoms):
        raise TypeError("A structure must be an ase.Atoms object")
    positions = np.asarray(atoms.positions, dtype=np.float64)
    numbers = np.asarray(atoms.numbers)
    cell = np.asarray(atoms.cell, dtype=np.float64)
    if positions.shape != (len(atoms), 3) or numbers.shape != (len(atoms),):
        raise ValueError("Invalid atomic array dimensions")
    if not np.isfinite(positions).all() or np.abs(positions).max(initial=0) > 1e15:
        raise ValueError("Positions must be finite and within 1e15 angstroms")
    if not np.isfinite(cell).all() or np.abs(cell).max(initial=0) > 1e15:
        raise ValueError("Cell vectors must be finite and within 1e15 angstroms")
    if np.any(numbers < 0) or np.any(numbers > 118):
        raise ValueError("Atomic numbers must be between 0 and 118")
    ids = atoms.arrays.get(ID_ARRAY)
    if ids is None or ids.shape != (len(atoms),) or ids.dtype.kind not in "iu":
        ids = np.zeros(len(atoms), dtype=np.int64)
    ids = ids.astype(np.int64, copy=True)
    used = set()
    next_id = max(_next_identity, int(ids.max(initial=0)) + 1)
    for i, value in enumerate(ids):
        if value <= 0 or value in used or value >= 2**52:
            ids[i] = next_id
            next_id += 1
        used.add(int(ids[i]))
    atoms.arrays[ID_ARRAY] = ids
    _next_identity = max(next_id, int(ids.max(initial=0)) + 1)
    # Atoms JSON preserves constraints, custom arrays and typed info. It does
    # not serialize an executable calculator, nor evaluate any properties.
    payload = encode(atoms)
    result = dict(positions=positions.tolist(), numbers=numbers.tolist(),
                  cell=cell.tolist(), pbc=atoms.pbc.tolist(), ids=ids.tolist(),
                  ase=payload, edits=[])
    if atoms.calc is not None and not atoms.calc.check_state(atoms):
        cached = atoms.calc.results
        energy = cached.get("energy")
        if energy is not None and np.isfinite(energy):
            result["energy"] = float(energy)
        forces = cached.get("forces")
        if forces is not None and np.shape(forces) == (len(atoms), 3) and np.isfinite(forces).all():
            result["forces"] = np.asarray(forces).tolist()
    return result


def unpack(snapshot):
    atoms = decode(snapshot["ase"]) if snapshot.get("ase") else Atoms(numbers=snapshot["numbers"])
    for edit in snapshot.get("edits", []):
        if "repeat" in edit:
            nx, ny, nz = edit["repeat"]
            n = len(atoms)
            atoms = atoms.repeat((nx, ny, nz))
            # ASE repeats x/y/z; the native model repeats z/y/x.
            order = [((x * ny + y) * nz + z) * n + i
                     for z in range(nz) for y in range(ny) for x in range(nx) for i in range(n)]
            atoms = atoms[order]
        else:
            atoms = atoms[edit["indices"]]
    if len(atoms) != len(snapshot["numbers"]):
        raise ValueError("ASE metadata and geometry have different atom counts")
    atoms.set_atomic_numbers(snapshot["numbers"])
    atoms.set_cell(snapshot["cell"], apply_constraint=False)
    atoms.set_pbc(snapshot["pbc"])
    atoms.set_positions(np.asarray(snapshot["positions"], dtype=float).reshape((-1, 3)),
                        apply_constraint=False)
    atoms.arrays[ID_ARRAY] = np.asarray(snapshot["ids"], dtype=np.int64)
    return atoms

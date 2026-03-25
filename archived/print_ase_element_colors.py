#!/usr/bin/env python3

"""Print ASE element RGB colors for each built-in element color scheme.

This script discovers color tables exposed by ``ase.data.colors`` that look like
per-element RGB arrays (for example ``jmol_colors`` and ``cpk_colors``), then
prints the RGB triplet for each element symbol.
"""

from __future__ import annotations

from typing import Iterable

import ase.data
import ase.data.colors as ase_colors
import numpy as np


def iter_element_color_schemes() -> Iterable[tuple[str, np.ndarray]]:
    """Yield all per-element RGB color tables exposed by ASE."""
    for name in sorted(dir(ase_colors)):
        if not name.endswith("_colors"):
            continue

        value = getattr(ase_colors, name)
        if not isinstance(value, np.ndarray):
            continue
        if value.ndim != 2 or value.shape[1] != 3:
            continue

        yield name.removesuffix("_colors"), value


def format_rgb(rgb: np.ndarray) -> str:
    return f"({rgb[0]:.6f}, {rgb[1]:.6f}, {rgb[2]:.6f})"


def main() -> None:
    chemical_symbols = ase.data.chemical_symbols
    schemes = list(iter_element_color_schemes())

    if not schemes:
        raise RuntimeError("No per-element RGB color tables found in ase.data.colors.")

    print(f"ASE version: {ase.__version__}")
    print("Discovered per-element ASE color schemes:")
    for scheme_name, table in schemes:
        print(f"  - {scheme_name} ({table.shape[0]} entries)")

    print()

    for scheme_name, table in schemes:
        print(f"=== {scheme_name.upper()} ===")
        print("Z   Symbol  RGB")
        print("--  ------  --------------------------------")

        for atomic_number in range(1, len(chemical_symbols)):
            symbol = chemical_symbols[atomic_number]
            if atomic_number < len(table):
                rgb = table[atomic_number]
                rgb_text = format_rgb(rgb)
            else:
                rgb_text = "N/A"

            print(f"{atomic_number:>2}  {symbol:<6}  {rgb_text}")

        print()


if __name__ == "__main__":
    import ase

    main()

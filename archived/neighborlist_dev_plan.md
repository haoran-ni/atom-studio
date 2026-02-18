# Neighbor List & Bond Detection — Development Plan

## Overview

We are replacing the existing O(n²) brute-force `BondList::detectBonds()` with a
proper **cell-list based neighbor list** that:

- Scales to large structures (O(n) build time)
- Correctly handles periodic boundary conditions (PBC)
- Uses covalent radii from `resources/covalent_radii.md` as the bond criterion
- Excludes atoms with no covalent radius (elements Bk and above, plus He, Ne, Ar, Kr, Xe, Rn)
- Exposes a user-tunable `scale` parameter (default 1.0) in the sidebar
- Rebuilds asynchronously on a background thread whenever the structure changes

---

## Core Concepts

### Atom Identity

Atoms are identified by their **0-based index into the Structure SoA arrays**. No
explicit ID field is stored. Index `i` corresponds to `posX[i]`, `posY[i]`,
`posZ[i]`, `atomicNumbers[i]`, etc.

### Bond Criterion

Two atoms `i` and `j` are bonded if:

```
distance(i, j) < (r_cov[i] + r_cov[j]) * scale
```

where `r_cov` is the covalent radius from `covalent_radii.md`. If either atom has
no covalent radius defined, the pair is skipped — no bond is formed.

### Scale Parameter

- Name: `scale` (was called `tolerance` in old code)
- Default: `1.0`
- Tunable by the user via a sidebar slider
- Stored in `RenderSettings` and passed to the neighbor list / bond detection

---

## NeighborList Data Structure

### Storage: CSR (Compressed Sparse Row)

```
m_neighbors: [ entry0, entry1, entry2, | entry3, entry4, | entry5, ... ]
m_offsets:   [  0,               3,        5,               ...        ]
```

- `m_offsets` has `n_atoms + 1` entries. `m_offsets[i]` is the start index of
  atom `i`'s neighbor entries in `m_neighbors`; `m_offsets[i+1]` is the end.
- `m_neighbors` is a flat array of `NeighborEntry`.

### NeighborEntry

```cpp
struct NeighborEntry {
    uint32_t index;   // SoA index of neighbor atom j
    int8_t   imageX;  // periodic image shift of j: actual pos = pos[j] + imageX*a + imageY*b + imageZ*c
    int8_t   imageY;
    int8_t   imageZ;
};
```

Image shifts are always (0, 0, 0) for non-PBC structures. For PBC, they are
integers in {-1, 0, 1} indicating which periodic image of `j` is the actual neighbor.

### Symmetry

The neighbor list is **symmetric**: if `j` is in `i`'s list, then `i` is in
`j`'s list (with negated image shifts). This means every bonded pair appears
**twice** in the flat array.

### Atoms Excluded from the Neighbor List

Atoms whose element has no defined covalent radius are excluded entirely from
`m_neighbors`. They are not listed as neighbors of any atom, and no other atom
appears in their neighbor list.

---

## Cell List Algorithm

### Step 1 — Compute global cutoff

```
global_cutoff = 2.0 * max_r_cov_present * scale
```

where `max_r_cov_present` is the largest covalent radius among elements that
actually appear in the structure AND have a defined covalent radius. This makes
the cell size tight to the actual structure rather than conservatively large.

### Step 2 — Define the grid

For non-PBC structures, use the bounding box. For PBC, use the simulation box.

```
nx = max(1, floor(box_x / global_cutoff))
ny = max(1, floor(box_y / global_cutoff))
nz = max(1, floor(box_z / global_cutoff))
cell_size_x = box_x / nx
cell_size_y = box_y / ny
cell_size_z = box_z / nz
```

Cells are sized so that `cell_size >= global_cutoff / max(nx,ny,nz)` — any bonded
pair is guaranteed to be in the same or immediately adjacent cells.

### Step 3 — Bin atoms into cells

For each atom `i` with a defined covalent radius:

```
ix = clamp(floor((x[i] - box_min_x) / cell_size_x), 0, nx-1)
iy = clamp(floor((y[i] - box_min_y) / cell_size_y), 0, ny-1)
iz = clamp(floor((z[i] - box_min_z) / cell_size_z), 0, nz-1)
cell_index = ix + iy*nx + iz*nx*ny
```

### Step 4 — Find neighbors (27-cell search)

For each atom `i`, iterate over the 27 cells `(ix+dx, iy+dy, iz+dz)` for
`dx, dy, dz ∈ {-1, 0, 1}`. For each candidate atom `j` in those cells:

1. Compute displacement vector `d = pos[j] - pos[i]`
2. For PBC: apply minimum image convention — find the nearest image of `j` to `i`
   using the lattice, record image shift `(nx, ny, nz)`
3. Compute `dist = |d|`
4. If `dist < global_cutoff` and `dist > 0`: add `j` to `i`'s neighbor list

**Why 27 cells is sufficient:** because `cell_size >= global_cutoff`, no bonded
atom can be more than 1 cell away in any dimension.

### PBC: Minimum Image Convention

For orthogonal cells, wrapping cell indices is trivial:
`(ix + dx + nx) % nx` and record `imageX = (ix + dx < 0) ? -1 : (ix + dx >= nx) ? +1 : 0`.

For triclinic cells, the minimum image is found by projecting the displacement
into fractional coordinates, rounding to the nearest integer, and subtracting.

---

## Bond Detection Pass

After the neighbor list is built, bond detection is a simple pass:

```
for each atom i:
    if r_cov[i] is undefined: skip
    for each NeighborEntry e in neighbors[offsets[i]..offsets[i+1]):
        j = e.index
        if j <= i: skip  // each pair once only (i < j convention)
        if r_cov[j] is undefined: skip
        threshold = (r_cov[i] + r_cov[j]) * scale
        if dist(i, j, e.image) < threshold:
            addBond(i, j, e.imageX, e.imageY, e.imageZ)
```

Bonds store the image shift so the renderer can draw cylinders to the correct
periodic image of the bonded atom.

---

## Updated Bond Struct

The existing `Bond` struct must gain image shift fields to support PBC rendering:

```cpp
struct Bond {
    uint32_t atomIndex1;   // always < atomIndex2 (primary atom)
    uint32_t atomIndex2;
    int8_t   imageX;       // image of atomIndex2 relative to atomIndex1
    int8_t   imageY;
    int8_t   imageZ;
    BondOrder order = BondOrder::Single;
};
```

---

## Asynchronous Rebuild

Triggered immediately whenever the structure changes (new file loaded, positions
updated). Implemented using `QtConcurrent::run()`:

1. **Main thread**: structure changes → call `NeighborList::rebuildAsync(structure, scale)`
2. **Worker thread**: builds cell list → fills CSR arrays → runs bond detection pass
3. **Main thread** (via Qt signal): receives new `BondList` → stores in `Structure`
   → signals renderer to re-upload bond instance data

A pending rebuild cancels any in-progress rebuild (via an atomic cancel flag).

---

## Files to Create

| File | Purpose |
|------|---------|
| `src/data/NeighborList.h` | `NeighborEntry` struct, `NeighborList` class declaration |
| `src/data/NeighborList.cpp` | Cell list build, CSR fill, PBC logic |

---

## Files to Modify

| File | Change |
|------|--------|
| `src/data/ElementData.h` | Change `covalentRadius` in `ElementInfo` from `float` to `std::optional<float>`; add `static std::optional<float> covalentRadius(int atomicNumber)` |
| `src/data/ElementData.cpp` | Replace covalent radii table with values from `resources/covalent_radii.md`; N/A entries → `std::nullopt` |
| `src/data/BondList.h` | Add `imageX/Y/Z` fields to `Bond`; update `detectBonds()` signature to take a `NeighborList` |
| `src/data/BondList.cpp` | Rewrite `detectBonds()` to use the neighbor list instead of O(n²) loop |
| `src/data/Structure.h` | Add `NeighborList` member; add async rebuild trigger |
| `src/data/Structure.cpp` | Implement async rebuild plumbing |
| `src/render/common/RenderSettings.h` | Add `float bondScale = 1.0f` |
| `src/ui/components/StructureModel.h/.cpp` | Expose `bondScale` Q_PROPERTY to QML |
| `src/ui/qml/Sidebar.qml` | Add `scale` slider for bonds |
| `src/render/opengl/BondRenderer.cpp` | Handle image shift when computing cylinder endpoints |
| `src/render/metal/MetalBondRenderer.mm` | Same — handle image shift |

---

## Implementation Progress

### Not Started
- (none)

### In Progress
- (none)

### Completed
- [x] `ElementData`: replaced covalent radii table; sentinel `-1.0f` for N/A; `std::optional<float>` exposed via static accessor
- [x] `NeighborList.h/.cpp`: full cell list with PBC, MIC, CSR storage
- [x] `BondList`: added image shift fields (`imageX/Y/Z`) to `Bond` struct; new `addBond()` signature; removed old `detectBonds()`
- [x] `Structure`: added `setBondList(shared_ptr<BondList>)`; `m_bonds` changed to `shared_ptr`
- [x] `OpenGLViewport`: `bondScale` Q_PROPERTY; async bond detection via `QtConcurrent::run` + `QFutureWatcher`
- [x] `MetalViewport`: same `bondScale` Q_PROPERTY and async detection pattern
- [x] `StructureModel`: added `notifyBondsUpdated()` slot (emits `structureChanged()`)
- [x] `Sidebar.qml`: Bond Scale slider (range 0.5–2.0, default 1.0)
- [x] `BondRenderer` (OpenGL): image shift applied to cylinder endpoint
- [x] `MetalBondRenderer`: image shift applied to cylinder endpoint
- [x] `CMakeLists`: `NeighborList.cpp` added to `atom-data`; `Qt6::Concurrent` added to `atom-ui`
- [x] Build verified — compiles clean (only expected macOS OpenGL deprecation warnings)

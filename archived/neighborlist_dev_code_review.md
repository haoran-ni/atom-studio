# Neighbor List / Bond Detection — Code Review 2: Findings & Solutions

Date: 2026-02-18
Scope: All findings from a full read of `NeighborList.h/cpp`, `BondList.h/cpp`,
`Structure.h/cpp`, `ElementData.h/cpp`, `BondRenderer.cpp`, and
`OpenGLViewport.h/cpp`.
Relation to first review (`neighborlist_dev_code_review.md`): Confirms all 5
prior findings, corrects their completeness, and adds 5 new findings.

---

## Summary Table

| # | Priority | Source | Short description |
|---|----------|--------|-------------------|
| 1 | **P1** | existing | `applyMIC` rounds all three axes — ignores `pbc[]` flags |
| 2 | **P1** | **new** | Noble gases (He/Ne/Ar/Kr/Xe/Rn) have positive covalent radii — form spurious bonds |
| 3 | **P1** | existing | Duplicate bonds/neighbor entries when any cell dimension = 1 |
| 4 | **P2** | **new** | `buildBondList` re-applies MIC into discarded dummy image accumulators |
| 5 | **P2** | existing (partial) | Stale bond list from old structure applied to new `m_structure` in `onBondsReady` |
| 6 | **P2** | existing | Sentinel −1 covalent radius leaks into rendered atom radii |
| 7 | **P2** | existing | `startBondDetection` spawns unbounded concurrent tasks |
| 8 | **P2** | **new** | `findBond`/`areBonded` ignore image shifts — broken for PBC structures |
| 9 | **P3** | existing | `setBondScale` unclamped — `scale ≤ 0` triggers division-by-zero in grid |
| 10 | **P3** | **new** | `covRadii[j] >= 0.0f` guard in `buildCellList` is dead code |

---

## Finding 1 (P1) — `applyMIC` wraps all axes regardless of `pbc[]` flags

### Problem

`NeighborList::applyMIC` (`src/data/NeighborList.cpp:27-29`) rounds all three
fractional components unconditionally:

```cpp
double ridx = std::round(frac[0]);   // always wraps X
double ridy = std::round(frac[1]);   // always wraps Y
double ridz = std::round(frac[2]);   // always wraps Z
```

`applyMIC` is called from **two** sites, both broken:
1. `buildCellList` line 243 — affects neighbor list contents and stored image shifts.
2. `buildBondList` line 319 — affects the distance used for the bond threshold.

The 27-cell per-axis wrapping in the grid loop (lines 186–219) correctly gates on
`lattice.pbc[k]`, making it inconsistent with `applyMIC`.

**Impact:** For slab (`pbc=[1,1,0]`) or wire (`pbc=[1,0,0]`) systems, `applyMIC`
applies a lattice translation along the non-periodic axis, producing phantom bonds
across the vacuum gap and incorrect image shifts for the renderer.

### Solution

Add `const std::array<bool, 3>& pbc` to `applyMIC`'s signature. Only round the
periodic fractional components:

```cpp
// NeighborList.h — updated declaration
static void applyMIC(
    float& dx, float& dy, float& dz,
    int8_t& imgX, int8_t& imgY, int8_t& imgZ,
    const Lattice& lattice,
    const std::array<bool, 3>& pbc);          // ← new parameter

// NeighborList.cpp — updated body
void NeighborList::applyMIC(
    float& dx, float& dy, float& dz,
    int8_t& imgX, int8_t& imgY, int8_t& imgZ,
    const Lattice& lattice,
    const std::array<bool, 3>& pbc)
{
    auto frac = lattice.cartesianToFractional(dx, dy, dz);

    double ridx = pbc[0] ? std::round(frac[0]) : 0.0;
    double ridy = pbc[1] ? std::round(frac[1]) : 0.0;
    double ridz = pbc[2] ? std::round(frac[2]) : 0.0;

    if (ridx != 0.0 || ridy != 0.0 || ridz != 0.0) {
        imgX = static_cast<int8_t>(static_cast<int>(imgX) - static_cast<int>(ridx));
        imgY = static_cast<int8_t>(static_cast<int>(imgY) - static_cast<int>(ridy));
        imgZ = static_cast<int8_t>(static_cast<int>(imgZ) - static_cast<int>(ridz));
        auto shift = lattice.fractionalToCartesian(ridx, ridy, ridz);
        dx -= static_cast<float>(shift[0]);
        dy -= static_cast<float>(shift[1]);
        dz -= static_cast<float>(shift[2]);
    }
}
```

Update both call sites to pass `lattice.pbc`:

```cpp
// buildCellList (line 243)
applyMIC(fdx, fdy, fdz, imgX, imgY, imgZ, lattice, lattice.pbc);

// buildBondList (line 319) — see also Finding 4
applyMIC(fdx, fdy, fdz, dummy0, dummy1, dummy2, lattice, lattice.pbc);
```

---

## Finding 2 (P1 — NEW) — Noble gases have positive covalent radii

### Problem

The dev plan states that He, Ne, Ar, Kr, Xe, and Rn must be excluded from bond
detection (sentinel `−1.0f`). The implementation gives them positive values
(`src/data/ElementData.cpp:19,28,37,56,75,108`):

```
He (Z=2):  0.28 Å     Ne (Z=10): 0.58 Å     Ar (Z=18): 1.06 Å
Kr (Z=36): 1.16 Å     Xe (Z=54): 1.40 Å     Rn (Z=86): 1.50 Å
```

Because `ElementData::covalentRadius()` returns `std::nullopt` only when
`covalentRadius < 0`, all six noble gases enter `activeAtoms` and participate in
neighbor search and bond detection. They will form spurious bonds (e.g., He–H,
Xe–F) in any structure containing them.

### Solution

Set the covalent radius to `−1.0f` for all six noble gases in `ElementData.cpp`:

```cpp
// Period 1
{2, "He", "Helium",  4.003f, -1.0f, 1.40f, Color::fromRgb(217, 255, 255)},

// Period 2
{10, "Ne", "Neon",   20.18f, -1.0f, 1.54f, Color::fromRgb(179, 227, 245)},

// Period 3
{18, "Ar", "Argon",  39.95f, -1.0f, 1.88f, Color::fromRgb(128, 209, 227)},

// Period 4
{36, "Kr", "Krypton", 83.80f, -1.0f, 2.02f, Color::fromRgb(92, 184, 209)},

// Period 5
{54, "Xe", "Xenon",  131.3f, -1.0f, 2.16f, Color::fromRgb(66, 158, 176)},

// Period 6
{86, "Rn", "Radon",  222.0f, -1.0f, 2.20f, Color::fromRgb(66, 130, 150)},
```

Their `vdwRadius` values are unchanged (used for atom display radius via
`radiusForElement`'s safe fallback). No change to `radiusForElement` is needed
since it already falls back to `vdwRadius` when `covalentRadius < 0`.

---

## Finding 3 (P1) — Duplicate neighbor entries and bonds when cell dimension = 1

### Problem

When any cell count `nx`, `ny`, or `nz` equals 1 (small periodic box or large
cutoff), all three offsets `{−1, 0, +1}` wrap to the same physical cell.
For example with `nz = 1`, each of `dz = {-1, 0, +1}` gives `jz = 0`.

For a neighbor atom `j` in the same cell as `i`, after the image displacement and
`applyMIC` are applied, all three `dz` iterations converge to the same corrected
displacement and thus produce the **identical** `NeighborEntry {j, imgX, imgY, imgZ}`.
That entry is pushed into `perAtom[i]` three times.

`buildBondList` then finds the same `j > i` entry three times and calls `addBond`
three times, inserting **three identical bonds** into the `BondList`.

**Impact:** Inflated bond counts, visual cylinder overlap/thickening, wasted GPU
draw calls.

### Solution

Deduplicate `perAtom[i]` once, just before CSR packing, by sorting and using
`std::unique`. This eliminates both the CSR bloat and the downstream duplicate
bonds in one step:

```cpp
// After all perAtom[i] vectors are filled, before CSR packing:
for (auto& neighbors : perAtom) {
    std::sort(neighbors.begin(), neighbors.end(),
        [](const NeighborEntry& a, const NeighborEntry& b) {
            if (a.index  != b.index)  return a.index  < b.index;
            if (a.imageX != b.imageX) return a.imageX < b.imageX;
            if (a.imageY != b.imageY) return a.imageY < b.imageY;
            return a.imageZ < b.imageZ;
        });
    neighbors.erase(
        std::unique(neighbors.begin(), neighbors.end(),
            [](const NeighborEntry& a, const NeighborEntry& b) {
                return a.index  == b.index  &&
                       a.imageX == b.imageX &&
                       a.imageY == b.imageY &&
                       a.imageZ == b.imageZ;
            }),
        neighbors.end());
}

// Then proceed to CSR packing as before.
```

This is O(k log k) per atom where k is the local neighbor count (typically ≤ 27),
so the overhead is negligible.

---

## Finding 4 (P2 — NEW) — `buildBondList` discards MIC image correction into dummy variables

### Problem

`buildBondList` (`src/data/NeighborList.cpp:319-321`) applies `applyMIC` a second
time using throwaway accumulators:

```cpp
int8_t dummy0 = 0, dummy1 = 0, dummy2 = 0;
applyMIC(fdx, fdy, fdz, dummy0, dummy1, dummy2, lattice);
```

Two problems:

1. **Image shifts discarded.** If `applyMIC` corrects the displacement (it
   shouldn't for a correctly built neighbor list, but the invariant is implicit),
   the correction is accumulated into `dummy0/1/2` and thrown away. The bond is
   then stored with `e->imageX/Y/Z` — the pre-MIC image — while the distance was
   computed with the post-MIC displacement. If MIC ever does fire here, the stored
   image shifts in the `Bond` struct will be inconsistent with the actual
   minimum-image vector used for the distance check, causing the renderer to draw
   cylinders to the wrong periodic image.

2. **Carries the P1 bug.** Even after P1 is fixed, this call still exists as dead
   work. Worse, before P1 is fixed, it applies full-axis MIC in `buildBondList`
   independently of the `applyMIC` fix in `buildCellList`, meaning partial-PBC
   is broken in the bond pass too.

### Solution

Remove the redundant `applyMIC` call entirely. The `NeighborEntry` already stores
MIC-corrected image shifts from `buildCellList`. The displacement reconstructed
from those stored images is, by construction, the minimum-image vector and needs
no further correction:

```cpp
// In buildBondList — REMOVE these three lines:
// int8_t dummy0 = 0, dummy1 = 0, dummy2 = 0;
// applyMIC(fdx, fdy, fdz, dummy0, dummy1, dummy2, lattice);

// Compute distance directly after applying stored image shift:
float dist = std::sqrt(fdx * fdx + fdy * fdy + fdz * fdz);
```

The `hasPBC` variable in `buildBondList` can also be removed since it is only
used to guard the now-deleted call.

---

## Finding 5 (P2) — Stale bond list applied to wrong structure in `onBondsReady`

### Problem

The existing review notes CPU waste from unbounded concurrent tasks (Finding 3 in
the first review). There is an additional correctness consequence not captured
there: if `setStructure(newStruct)` is called while a bond detection task is
still running for `oldStruct`, `m_structure` is replaced before the task
completes. `onBondsReady` (`src/ui/components/OpenGLViewport.cpp:370-371`) then
applies the old structure's bond topology to the new structure:

```cpp
// m_structure may now point to a different structure than the one
// the completed task was built from.
m_structure->setBondList(std::move(newBonds));
```

The new structure silently gets wrong bond connectivity.

### Solution

Return the bond result together with the **originating structure pointer** from the
worker, and verify it against `m_structure` in `onBondsReady`. Combine with the
at-most-one coalescing fix for Finding 7.

A request-ID alone is insufficient: when a new structure arrives while a task is
running, the new request goes into the *pending* queue without launching a new
task, so `m_bondRequestId` is not incremented. When the running task finishes,
`result.requestId == m_bondRequestId` passes — but `m_structure` is already the
new structure. Binding the originating `structure` shared_ptr into `BondResult`
closes this gap, because pointer equality is an unambiguous identity check
regardless of when IDs are incremented.

Add a `BondResult` type and the coalescing state:

```cpp
// OpenGLViewport.h (private section)
struct BondResult {
    std::shared_ptr<data::BondList>  bonds;
    std::shared_ptr<data::Structure> structure;  // originating structure
};
QFutureWatcher<BondResult>* m_bondWatcher = nullptr;
bool      m_bondTaskRunning  = false;
bool      m_bondTaskPending  = false;
std::shared_ptr<data::Structure> m_pendingStructure;
float     m_pendingScale     = 1.0f;

void startBondDetection();
void launchBondTask(std::shared_ptr<data::Structure> structure, float scale);
```

```cpp
// OpenGLViewport.cpp

void OpenGLViewport::startBondDetection() {
    if (!m_structure) return;
    if (m_bondTaskRunning) {
        // Coalesce: remember latest parameters, launch after current task ends.
        m_bondTaskPending  = true;
        m_pendingStructure = m_structure;
        m_pendingScale     = m_bondScale;
        return;
    }
    launchBondTask(m_structure, m_bondScale);
}

void OpenGLViewport::launchBondTask(
    std::shared_ptr<data::Structure> structure, float scale)
{
    if (!m_bondWatcher) {
        m_bondWatcher = new QFutureWatcher<BondResult>(this);
        connect(m_bondWatcher, &QFutureWatcher<BondResult>::finished,
                this, &OpenGLViewport::onBondsReady);
    }
    m_bondTaskRunning = true;
    m_bondTaskPending = false;

    m_bondWatcher->setFuture(
        QtConcurrent::run([structure, scale]() -> BondResult {
            data::NeighborList nl;
            nl.build(*structure, scale);
            return {nl.buildBondList(*structure, scale), structure};
        }));
}

void OpenGLViewport::onBondsReady() {
    m_bondTaskRunning = false;

    if (m_bondWatcher && m_structure) {
        BondResult result = m_bondWatcher->result();
        // Discard if the structure has changed since the task was launched.
        if (result.structure == m_structure && result.bonds) {
            m_structure->setBondList(std::move(result.bonds));
            m_needsStructureUpdate = true;
            emit bondCountChanged();
            if (auto* model = StructureModel::instance())
                model->notifyBondsUpdated();
            update();
        }
    }

    if (m_bondTaskPending && m_pendingStructure) {
        auto s  = std::move(m_pendingStructure);
        float sc = m_pendingScale;
        launchBondTask(std::move(s), sc);
    }
}
```

The same `BondResult` type and `launchBondTask`/`onBondsReady` pattern must be
applied identically to **`MetalViewport`** (`src/ui/components/MetalViewport.mm`)
which has the same async bond detection code path.

---

## Finding 6 (P2) — Sentinel −1 radius leaks into rendered atom radii

### Problem

Two call sites in `src/data/Structure.cpp` write `elem.covalentRadius` directly
into `m_radii` without guarding against the `−1.0f` sentinel:

- `addAtom` line 253: `m_radii.push_back(elem.covalentRadius);`
- `updateRadiiFromElements` line 327: `float r = ... elem.covalentRadius; m_radii[i] = r * scale;`

For elements Z ≥ 97 (Bk and above) this pushes `−1.0f` into the radius buffer,
producing negative atom display radii. Depending on shader assumptions, this can
invert impostor sphere depth, cause atoms to disappear, or produce GPU NaNs.

### Solution

Replace both raw accesses with `ElementData::radiusForElement()`, which already
has the correct safe fallback to `vdwRadius` when `covalentRadius < 0`:

```cpp
// addAtom (Structure.cpp ~line 253)
m_radii.push_back(ElementData::radiusForElement(atomicNumber, false));

// updateRadiiFromElements (Structure.cpp ~line 327)
m_radii[i] = ElementData::radiusForElement(m_atomicNumbers[i], useVdW) * scale;
```

No changes needed to `ElementData.h/.cpp`; `radiusForElement` is already correct.

---

## Finding 7 (P2) — `startBondDetection` spawns unbounded concurrent tasks

### Problem

`startBondDetection` (`OpenGLViewport.cpp:341-363`) launches a new
`QtConcurrent::run` task on every call, replacing `m_bondWatcher`'s future with
`setFuture()`. The old task is not cancelled — it runs to completion in the
background. Under rapid slider movement or rapid file-open events on a large
structure, many tasks pile up, contending for CPU cores and holding shared_ptr
references to potentially large structure data.

### Solution

The at-most-one coalescing pattern described in Finding 5's solution (`m_bondTaskRunning` / `m_bondTaskPending` / `launchBondTask`) directly fixes this: at most one `QtConcurrent` task runs at any time. Superseded requests are coalesced into a single pending record that is launched immediately when the running task completes.

No additional changes beyond the Finding 5 solution are required.

---

## Finding 8 (P2 — NEW) — `findBond` / `areBonded` ignore periodic image shifts

### Problem

`BondList::findBond` (`src/data/BondList.cpp:38-48`) compares only `atomIndex1`
and `atomIndex2`, not `imageX/Y/Z`:

```cpp
if (m_bonds[i].atomIndex1 == atomIndex1 &&
    m_bonds[i].atomIndex2 == atomIndex2) {
    return static_cast<int>(i);
}
```

For a small PBC cell where the same atom pair is bonded in two distinct periodic
images (e.g., atom 0 bonded to atom 1 directly **and** across the boundary), this
returns the first match regardless of which image is actually queried. This makes
`areBonded` incorrect for such structures and will silently break any future
deduplication that calls `areBonded` to avoid inserting a bond.

### Solution

Add default `imageX/Y/Z = 0` parameters to `findBond` and compare them:

```cpp
// BondList.h
/** @return Bond index, or -1 if not found. */
int findBond(uint32_t atomIndex1, uint32_t atomIndex2,
             int8_t imageX = 0, int8_t imageY = 0, int8_t imageZ = 0) const;

bool areBonded(uint32_t atomIndex1, uint32_t atomIndex2,
               int8_t imageX = 0, int8_t imageY = 0, int8_t imageZ = 0) const;

// BondList.cpp
int BondList::findBond(uint32_t a1, uint32_t a2,
                       int8_t imgX, int8_t imgY, int8_t imgZ) const
{
    if (a1 > a2) { std::swap(a1, a2); imgX = -imgX; imgY = -imgY; imgZ = -imgZ; }
    for (size_t i = 0; i < m_bonds.size(); ++i) {
        if (m_bonds[i].atomIndex1 == a1 &&
            m_bonds[i].atomIndex2 == a2 &&
            m_bonds[i].imageX     == imgX &&
            m_bonds[i].imageY     == imgY &&
            m_bonds[i].imageZ     == imgZ) {
            return static_cast<int>(i);
        }
    }
    return -1;
}
```

Callers that omit the image parameters get the non-PBC default (all zeros), which
is backward-compatible.

---

## Finding 9 (P3) — `setBondScale` unclamped — can trigger division-by-zero

### Problem

`OpenGLViewport::setBondScale` (`OpenGLViewport.cpp:332-338`) does not validate
its input. With `scale ≤ 0`:

- `globalCutoff = 2.0f * maxCovRadius * scale` becomes zero or negative.
- `nx = max(1, floor(boxX / globalCutoff))` → `floor(boxX / 0)` → integer
  division by zero / `INT_MAX` → undefined behaviour.

The QML slider constrains the UI path, but the C++ API is open to non-QML
callers.

### Solution

Apply the same fix in **both** viewport setters — `OpenGLViewport` and
`MetalViewport` (`src/ui/components/MetalViewport.mm:299`) have identical
`setBondScale` implementations and identical exposure:

```cpp
// Apply to OpenGLViewport::setBondScale AND MetalViewport::setBondScale
void XxxViewport::setBondScale(float scale) {
    if (!std::isfinite(scale)) return;
    scale = std::clamp(scale, 0.1f, 5.0f);   // product-defined bounds
    if (qFuzzyCompare(m_bondScale, scale)) return;
    m_bondScale = scale;
    emit bondScaleChanged();
    if (m_structure) startBondDetection();
}
```

Additionally, add a **mandatory** guard at the top of `NeighborList::build`. The
setter guard protects only the Qt UI path; `NeighborList::build` is a public API
that can be called directly by tests, Python bindings, or future callers. A bad
scale value passed directly to `build` still triggers UB at line 83 without this
guard:

```cpp
// NeighborList.cpp — top of build()
void NeighborList::build(const Structure& structure, float scale) {
    if (!std::isfinite(scale) || scale <= 0.0f) {
        // Clear to empty neighbor list; caller can treat as "no bonds".
        m_offsets.assign(structure.atomCount() + 1, 0u);
        m_neighbors.clear();
        return;
    }
    // ... rest of build unchanged
}
```

---

## Finding 10 (P3 — NEW) — Dead guard in `buildCellList`

### Problem

`buildCellList` (`src/data/NeighborList.cpp:250`) guards the neighbor entry push
with:

```cpp
if (covRadii[j] >= 0.0f) {
    perAtom[i].push_back({j, imgX, imgY, imgZ});
}
```

`cellAtoms` is populated exclusively from `activeAtoms` — atoms for which
`covRadii[i] >= 0` was already verified. Any `j` found in `cellAtoms[jCell]` is
guaranteed to have a non-negative covalent radius. The guard is always `true` and
is dead code.

### Solution

Remove the guard. A clarifying comment is sufficient:

```cpp
// j is from cellAtoms which contains only active atoms (defined covalent radius).
perAtom[i].push_back({j, imgX, imgY, imgZ});
```

---

## Prioritised Implementation Order

1. **Fix `applyMIC` per-axis PBC** (Finding 1) — correctness blocker for all
   partial-PBC systems.
2. **Fix noble gas covalent radii to −1** (Finding 2) — correctness blocker for
   any structure containing He/Ne/Ar/Kr/Xe/Rn.
3. **Deduplicate `perAtom` before CSR packing** (Finding 3) — correctness blocker
   for small periodic cells.
4. **Remove redundant MIC in `buildBondList`** (Finding 4) — removes a fragile
   and misleading code pattern; eliminates a second P1-affected site.
5. **Fix `setBondScale` clamping** (Finding 9) — prevents UB crash path.
6. **Fix sentinel radius leak in `addAtom` and `updateRadiiFromElements`**
   (Finding 6) — prevents negative display radii.
7. **Add request-ID + at-most-one task coalescing** (Findings 5 & 7 combined) —
   fixes stale result + CPU waste in one change.
8. **Fix `findBond`/`areBonded` image awareness** (Finding 8) — needed before
   any deduplication logic calls these functions.
9. **Remove dead `covRadii[j]` guard** (Finding 10) — cleanup.

---

## Implementation Progress

| Step | Finding(s) | Status | Notes |
|------|------------|--------|-------|
| 1 | 1 — `applyMIC` per-axis PBC | ✅ done | |
| 2 | 2 — Noble gas covalent radii → −1 | ✅ done | |
| 3 | 3 — `perAtom` deduplication | ✅ done | |
| 4 | 4 — Remove redundant MIC in `buildBondList` | ✅ done | |
| 5 | 9 — `setBondScale` + `build()` guard (OpenGL + Metal) | ✅ done | |
| 6 | 6 — Sentinel radius leak in `addAtom` / `updateRadiiFromElements` | ✅ done | |
| 7 | 5+7 — Structure-bound `BondResult` + at-most-one coalescing (OpenGL + Metal) | ✅ done | |
| 8 | 8 — `findBond`/`areBonded` image-shift comparison | ✅ done | |
| 9 | 10 — Remove dead `covRadii[j]` guard | ✅ done | |

**Legend:** ⬜ pending · 🔄 in progress · ✅ done · ❌ blocked

---

## Files Changed

| File | Findings addressed |
|------|-------------------|
| `src/data/NeighborList.h` | 1 (applyMIC signature) |
| `src/data/NeighborList.cpp` | 1 (applyMIC body + call sites), 3 (perAtom dedup), 4 (remove redundant MIC) |
| `src/data/ElementData.cpp` | 2 (noble gas covalent radii → −1) |
| `src/data/BondList.h` | 8 (findBond/areBonded image params) |
| `src/data/BondList.cpp` | 8 (findBond/areBonded image comparison) |
| `src/data/Structure.cpp` | 6 (addAtom, updateRadiiFromElements use radiusForElement) |
| `src/ui/components/OpenGLViewport.h` | 5+7 (BondResult type, new members) |
| `src/ui/components/OpenGLViewport.cpp` | 5+7 (launchBondTask, onBondsReady structure-identity check), 9 (setBondScale clamping) |
| `src/ui/components/MetalViewport.h` | 5+7 (BondResult type, new members) |
| `src/ui/components/MetalViewport.mm` | 5+7 (launchBondTask, onBondsReady structure-identity check), 9 (setBondScale clamping) |

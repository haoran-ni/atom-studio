# Development Log

This file records development sessions and decisions for future reference.

---

## 2026-02-06: Fix Impostor Sphere Rendering for Large/Overlapping Atoms

### Summary
Fixed two rendering bugs in the impostor sphere shaders that caused visual artifacts when atom sizes were large or spheres overlapped. The billboard quads were too small under perspective projection, and the ray-sphere intersection used an orthographic approximation instead of proper perspective rays.

### Files Modified
| File | Change |
|------|--------|
| `src/render/opengl/ShaderManager.cpp` | Rewrote sphere vertex shader (perspective-correct billboard sizing) and fragment shader (perspective ray-sphere intersection) |
| `src/render/opengl/SphereRenderer.h` | Removed unused `viewportWidth`/`viewportHeight` params from `render()` |
| `src/render/opengl/SphereRenderer.cpp` | Updated `render()` signature; removed `uViewportSize` uniform set |
| `src/render/opengl/OpenGLRenderer.cpp` | Updated `render()` call site to match new signature |

### Architecture Decisions

#### 1. Perspective-Correct Billboard Sizing
Previously, the billboard quad was expanded by a fixed `1.2 * R` factor in view space. Under perspective, the projected silhouette of a sphere at distance `d` is `R * d / sqrt(d^2 - R^2)`, which exceeds `1.2R` when `R > 0.55d`. The vertex shader now computes this exact formula, with a 1.05x safety margin and a fallback for degenerate cases (camera inside sphere).

#### 2. Perspective Ray-Sphere Intersection
Previously, the fragment shader used an orthographic approximation: `d = quadCoord * R; z = sqrt(R^2 - d^2)`. This assumes all view rays are parallel to the z-axis, which is wrong under perspective. Now, each fragment traces a ray from the eye `(0,0,0)` through the interpolated view-space billboard position, and solves the standard quadratic `t^2 - 2t(D.C) + |C|^2 - R^2 = 0`. This gives correct normals, shapes, and depth at all overlap regions.

#### 3. Camera-Inside-Sphere Handling
When the front intersection `t` is negative (camera inside sphere), the shader falls back to the back intersection `t = b + sqrt(disc)`, rendering the interior correctly.

#### 4. Removed Unused `uViewportSize`
The viewport size uniform and corresponding render parameters were vestigial — neither the old nor new shader logic required them.

---

## 2026-02-06: Fix Atom Scale Slider and Use Original Covalent Radii

### Summary
Connected the Atom Scale sidebar slider to the rendering pipeline and removed all hard-coded 0.5x radius scaling so atoms render at their true covalent radii by default. Fixed broken QML cross-file id resolution that prevented all sidebar controls from reaching the viewport.

### Files Modified
| File | Change |
|------|--------|
| `src/data/Structure.cpp` | `addAtom()` uses `elem.covalentRadius` directly (was `* 0.5f`) |
| `src/python/ASEReader.cpp` | `updateRadiiFromElements(1.0f, ...)` in both `readFile()` and `readFromString()` (was `0.5f`) |
| `src/render/RenderSettings.h` | `atomScale` default changed to `1.0f` (was `0.5f`) |
| `src/render/opengl/ShaderManager.cpp` | Added `uniform float uAtomScale` to sphere vertex shader; radius = `aInstancePos.w * uAtomScale` |
| `src/render/opengl/SphereRenderer.cpp` | Sets `uAtomScale` uniform from `settings.atomScale` each frame |
| `src/ui/components/OpenGLViewport.h` | `m_atomScale` default changed to `1.0f` (was `0.5f`) |
| `src/ui/qml/Sidebar.qml` | Added `property var viewport: null`; all handlers use `sidebar.viewport` instead of broken `mainWindow.viewportPanel.viewport` chain; slider default `1.0` |
| `src/ui/qml/Main.qml` | Passes `viewport: viewportPanel.viewport` to Sidebar |

### Architecture Decisions

#### 1. No Hidden Scaling
Previously, atom radii were scaled to 50% in three places: `addAtom()`, `updateRadiiFromElements()`, and `RenderSettings::atomScale`. Now all defaults are 1.0 — the raw covalent radii from `ElementData` are used, and the user controls visual scale via the sidebar slider.

#### 2. GPU-side Atom Scale
The `uAtomScale` uniform is applied in the sphere vertex shader (`vRadius = aInstancePos.w * uAtomScale`), so changing the slider does not require re-uploading buffer data — only a uniform update per frame.

#### 3. Explicit Viewport Property for QML Wiring
The original QML code accessed the viewport via `mainWindow.viewportPanel.viewport` — a cross-file id chain that doesn't resolve in Qt 6's component scoping. Fixed by passing the viewport as an explicit property (`property var viewport`) from `Main.qml` to `Sidebar.qml`. This also fixed the Show Bonds checkbox and Reset Camera / Fit to View buttons.

---

## 2026-02-06: Add Unit Cell Wireframe Visualization

### Summary
Added unit cell visualization to the interactive OpenGL viewport. The unit cell is rendered as 12 wireframe edges of the parallelepiped defined by the lattice vectors, using simple `GL_LINES` — no lighting or ray tracing.

### Files Created
| File | Purpose |
|------|---------|
| `src/render/opengl/UnitCellRenderer.h` | Unit cell wireframe renderer interface |
| `src/render/opengl/UnitCellRenderer.cpp` | Computes 8 corners & 12 edges from lattice vectors, draws with GL_LINES |

### Files Modified
| File | Change |
|------|--------|
| `src/render/opengl/OpenGLRenderer.h` | Added `UnitCellRenderer` member, accessor, and dirty flag |
| `src/render/opengl/OpenGLRenderer.cpp` | Initialize, cleanup, data upload, and render call for unit cell |
| `src/render/CMakeLists.txt` | Added UnitCellRenderer source files |

### Architecture Decisions

#### 1. Simple Line Rendering
Unit cell edges are drawn with `GL_LINES` using the existing line shader (`uViewProjectionMatrix` only). No lighting, no instancing — just 8 vertices and 12 indexed line segments.

#### 2. Parallelepiped from Lattice Vectors
The 8 corners are computed from the 3 lattice vectors (a, b, c) stored in `Structure::Lattice::matrix[3][3]`:
- Origin, A, B, C, A+B, A+C, B+C, A+B+C

#### 3. Render Order
Unit cell lines are drawn first (before bonds and atoms) so they appear behind the structure with correct depth testing.

#### 4. Conditional Rendering
- Only rendered when `Structure::hasLattice()` is true (i.e. cell volume > 0)
- Toggled via `RenderSettings::showUnitCell` (default: true)
- Color and line width from `RenderSettings::unitCellColor` and `unitCellLineWidth`

---

## 2026-02-05: Replace Native File Readers with Python ASE Integration

### Summary
Replaced all C++ native file readers (XYZ, LAMMPS, CIF) with Python ASE integration using pybind11 embedded interpreter. ASE supports 70+ atomic structure file formats.

### Commits
- `181a115` - feat: replace native file readers with Python ASE integration

### Files Created
| File | Purpose |
|------|---------|
| `src/python/PythonRuntime.h` | Python interpreter lifecycle management (singleton) |
| `src/python/PythonRuntime.cpp` | Init/finalize, GIL guards, bundled Python detection |
| `src/python/ASEReader.h` | ASE-based file reader interface |
| `src/python/ASEReader.cpp` | Reads files via `ase.io.read()`, converts to Structure |
| `src/python/CMakeLists.txt` | Build config for python module |
| `src/data/Structure.h` | New Structure class with SoA layout |
| `src/data/Structure.cpp` | Structure implementation |
| `cmake/BundlePython.cmake` | Script to bundle Python + ASE into app |

### Files Deleted
- `src/io/readers/XYZReader.{h,cpp}`
- `src/io/readers/LAMMPSDumpReader.{h,cpp}`
- `src/io/readers/CIFReader.{h,cpp}`
- `src/data/AtomicStructure.{h,cpp}`
- `src/data/UnitCell.{h,cpp}`

### Architecture Decisions

#### 1. Python GIL Management
- Python initialized once at startup in `main.cpp`
- Main thread releases GIL after init (`PyEval_SaveThread`)
- Worker threads acquire GIL via RAII `GILGuard` class
- All pybind11 objects must be destroyed before releasing GIL

```cpp
// Pattern for worker threads
{
    PythonRuntime::GILGuard gil;
    py::module_ ase_io = py::module_::import("ase.io");
    // ... use Python objects ...
}  // GIL released here
```

#### 2. ASE Pre-loading
ASE is imported at startup (not on first file open) to avoid delay:
```cpp
// In PythonRuntime::initialize()
{
    GILGuard gil;
    py::module_ ase_io = py::module_::import("ase.io");  // Pre-load
}
```

#### 3. No Extension Filtering
ASE supports 70+ formats, so we don't filter by extension:
- `supportedExtensions()` returns `{"*"}` (wildcard)
- `canRead()` only checks if file exists
- Let ASE try to read any file; it reports errors for unsupported formats

#### 4. Bundled Python
For deployment, Python is bundled into the app:
- macOS: `Contents/Resources/python/lib/python3.X/`
- Windows: `python/Lib/`
- Linux: `python/lib/python3.X/`

Build with: `cmake --build build --target deploy`

### Technical Issues Resolved

#### Issue 1: PyGILState_STATE Type Conflict
**Problem**: Header defined `using PyGILState_STATE = int;` which conflicted with Python.h's enum.
**Solution**: Changed to `int m_state;` in GILGuard, cast at usage points.

#### Issue 2: pybind11 Implicit Conversion
**Problem**: `atoms.attr("positions")` returns accessor, not array.
**Solution**: Add explicit `.cast<py::array_t<double>>()`.

#### Issue 3: GIL Error on Exit
**Problem**: pybind11 objects destroyed without GIL held.
**Solution**: Add nested scope to ensure objects destroyed before `PyEval_SaveThread()`.

#### Issue 4: "No reader available for file" Error
**Problem**: Extension map only had "*" but lookup used actual extension.
**Solution**: Check `canRead()` first in `readerForPath()`, then fallback to extension lookup with wildcard support.

### Dependencies
- pybind11 (via vcpkg)
- Python 3.8+ with NumPy
- ASE (`pip install ase`)

### Build Commands
```bash
# Regular build (uses system Python)
cmake --preset default
cmake --build build

# Deploy with bundled Python (for distribution)
cmake --build build --target deploy
```

### Testing
Run the app and open various file formats:
```bash
./build/bin/atom-studio.app/Contents/MacOS/atom-studio
```

Supported formats include: XYZ, CIF, LAMMPS dump/data, VASP POSCAR/CONTCAR, PDB, Gaussian, Quantum ESPRESSO, ASE traj/json/db, and 60+ more.

---

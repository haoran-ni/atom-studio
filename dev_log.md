# Development Log

This file records development sessions and decisions for future reference.

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

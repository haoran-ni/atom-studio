# GUIDELINES.md

This file provides guidance to AI agent when working with code in this repository.
AI agent can modify this file as appropriate.

## Project Overview

ATOM-STUDIO is a high performance application (not a web application, but a desktop application) for nice visualization of atomic structures. It includes most of the functionalities in OVITO, but is better than OVITO. Some of its highlight features include:
1. An interactive visualization panel
2. Highly optimized real-time ray tracing renderer
3. Supports other renderers
4. GPU optimized
5. CPU optimized (parallel computing on CPU)
6. Supports the input of various atomic structure file types.
7. Executable on Windows, MacOS and Linux.

The application consists of a header bar for app options, a side bar showing information, and an interactive visualization panel. The sizes and positions of the visualization panel and the side bar can be easily adjusted.

## Dev requirements
1. The code should be very modulized, meaning that its functionalities are written in a very clean way, and are assembled with each other outside the class/function definiton of that functionality.
2. The code should be highly optimized and fast to run.
3. Whenever you met something you can not decide, ask!

## Current UI style requirements
- The active custom visual language currently applies primarily to the **sidebar**, not to the whole application shell.
- Sidebar styling should stay in `src/ui/qml/Sidebar.qml` unless there is a clear reason to promote something into a shared reusable component.
- The sidebar should use a **bright neutral palette**: white, light gray, dark gray, and black. Avoid dark navy / purple styling in new sidebar work unless explicitly requested.
- Sidebar section icons and expand/collapse arrows should come from `resources/icons/` SVG assets, exposed through `resources/resources.qrc`, rather than placeholder text glyphs or improvised canvas icons.
- Sidebar tab headers must keep **consistent visual sizing** between collapsed and expanded states:
  - no font magnification on expand
  - no icon magnification on expand
  - no arrow magnification on expand
  - fixed header height between states
- Collapsed sidebar tabs should appear as clean list items without extra card borders; expanded tabs may use a subtle highlighted background.
- Hierarchy connector lines in the sidebar are only for **tunable parameter blocks** and action groups, not for purely informational sections such as `Structure Info`.
- Sidebar hierarchy connectors must:
  - connect smoothly to the backbone with no visible gaps
  - align to the vertical center of the rendered parameter block they point to
  - stop cleanly at the last parameter block with no extra tail
  - remain visually aligned with the corresponding option text/control block
- When editing sidebar visuals, preserve existing control behavior and viewport wiring unless the task explicitly asks for behavioral changes.

## Application Branding
- The runtime application/window icon should be loaded from the Qt resource system, currently `qrc:/branding/app-logo.png`, configured in `resources/resources.qrc` and applied in `src/main.cpp`.
- The macOS bundle icon shown by Finder/Dock should come from `resources/logo_1024x1024.icns`, packaged via `CMakeLists.txt` and referenced through `resources/Info.plist.in`.
- When updating app branding, keep the runtime Qt icon path and the macOS bundle icon configuration in sync.

## Technology Stack

### Language: C++17/20
- Core engine in modern C++
- Embedded Python (pybind11) for file I/O via ASE
- C API for external bindings

### UI Framework: Qt 6
- Qt Widgets/QML for UI components
- Dedicated render surface/widget per graphics backend
- Decoupled UI event loop from render loop (explicit frame scheduling)
- Own renderer abstraction layer

### Graphics API: Backend-Based Architecture
The rendering system is organized by **graphics backend**, not by OS. Shared abstractions live in `src/render/common/`, and each backend has its own subfolder.

- **OpenGL backend** (`src/render/opengl/`): Fallback on all platforms
  - Raster renderer: instanced impostor spheres
  - Fragment-shader ray tracing with progressive accumulation
- **Vulkan backend** (`src/render/vulkan/`): Future — primary for Windows/Linux
  - Hardware-accelerated ray tracing (BLAS/TLAS)
  - Ray tracing pipeline and ray queries
- **Metal backend** (`src/render/metal/`): Current primary for macOS
  - Raster renderer: instanced impostor spheres
  - Fragment-shader ray tracing with progressive accumulation (unified BVH for atoms + bonds)

### Platform Strategy
- **macOS**: Metal (current primary), OpenGL 4.1 (fallback)
- **Windows**: OpenGL (current), Vulkan + RT (future primary)
- **Linux**: OpenGL (current), Vulkan + RT (future primary)
- CMake detects the platform and builds only the relevant backends

### Build System: CMake
- vcpkg or Conan for dependency management
- Platform-conditional backend compilation in `src/render/CMakeLists.txt`
- Cross-platform CI pipeline
- Normal development builds must be self-consistent: `cmake --build build` should leave the app runnable without requiring a separate manual Python deployment step.
- If embedded/bundled Python is used, the build system must automatically invalidate and rebuild stale bundled runtimes when the selected interpreter version, ABI, or package set changes.
- macOS builds do not require the historical AGL stub or changes to `/opt/homebrew/lib`.
- macOS Python bundling includes the interpreter shared library and the recursive native dependency graph. Only bundle copies may be relocated or signed; never modify the source Python installation.
- Validate macOS Python using the Qt-free embedded-runtime check, including native library origins. Running the developer's Python against copied packages is insufficient evidence of portability.

### File I/O: Python ASE Integration
- Uses ASE (Atomic Simulation Environment) via embedded Python
- Supports 70+ file formats out of the box (XYZ, CIF, LAMMPS, VASP, PDB, etc.)
- Architecture:
  - Python initialized at startup, ASE pre-loaded
  - Worker threads acquire GIL for file reading
  - ASE reads files → converted to C++ Structure with zero-copy numpy access
  - Bundled Python distribution for deployment (no system Python required)
  - The runtime must only use a bundled Python stdlib that exactly matches the embedded interpreter version; mismatched bundles must be rejected and regenerated by the build.
  - On macOS, initialize Python with isolated `PyConfig` and explicit bundle paths. Do not import external user packages, execute site customization, change Python environment variables, or fall back to a system interpreter. Keep bytecode writes disabled inside the signed app bundle.

### Multiple Structures
- `StructureModel` owns a collection of `StructureDocument` entries. Each entry has a stable, monotonically increasing session ID, immutable `STRUCT_<ID>_RAW`, independent `STRUCT_<ID>_CURRENT`, selections, and lazy appearance/bond state.
- `FileController` appends successful imports through `addStructure()` and queues batch/repeated requests. `setStructure()` explicitly replaces a session for existing programmatic callers.
- Keep a single backend viewport and camera. `structureActivated` recenters on the active entry (unit-cell center, or geometric atom center without a lattice), preserving orientation, zoom, projection and pan relative to its center. `Camera::setSceneCenter()` translates the target without changing those view controls; `structureUpdated` handles explicit geometry resets.
- Replication factors belong to the model and apply to all imports, including future imports. Stage all periodic working copies before committing, preserve nonperiodic edits, and notify the viewport once. Resetting one document rebuilds it from raw with the global factors. Auto-fit uses the collection's maximum view extent; appearance/bond work for inactive entries stays lazy.
- Global appearance defaults are applied lazily on activation. Preserve explicit per-object color and bond-radius overrides; raw structures are never styled or edited.
- Bond detection belongs to `StructureModel`, uses a snapshot, and rejects obsolete revisions even for A → B → A switches. Superseded bond/BVH work supports cooperative cancellation.
- Release prior GPU scene data on structure switches, reject obsolete Metal completions, and restart RT accumulation even for geometrically identical entries. Keep the last displayed texture/node until its replacement is ready; reserve its output slot against GPU writes to prevent flicker.
- `StructureSwitcher.qml` is a UI overlay, visible only for multiple entries, excluded from image capture. Defer activation while image export holds `switchingLocked` so it captures the requested structure.

### Structure Export
- Native geometry writers in `src/io/StructureFileWriter` support FHI-aims `.in`, CIF, POSCAR, and extended XYZ; ASE independently checks the exported files in tests.
- Export a snapshot of `StructureModel::structure()` (the current working structure), including deletion and replication edits. Do not export `originalStructure()` or renderer coordinates.
- Copy only geometry needed by the writer, then serialize in a worker thread using `QSaveFile` for atomic replacement. Do not access the mutable UI model from the worker.
- Preserve unwrapped positions. CIF uses explicit P1 sites; POSCAR groups atoms by element; extended XYZ preserves lattice and PBC metadata.
- When no lattice is defined, POSCAR must report an error; the other formats must omit lattice records without inventing a cell.

### Interactive Python
- `PythonShellController` owns a persistent process using a user-owned venv created
  from the bundled Qt-free `PythonLauncher`. `PythonEnvironmentManager` handles
  preparation, package operations and external terminal launching; keep package UI
  in `PythonPackagesWindow`. Native file imports retain the isolated embedded runtime.
  The old `--python-shell-worker` entry remains available for embedded diagnostics.
- User packages belong outside the signed bundle. Venvs may inherit bundled packages
  and install local overrides; never pip-install into the base runtime. Key environment
  storage by Python version/architecture and repair launcher links after app moves.
  Use real subprocesses for pip, argument lists rather than shell interpolation, and
  serialize environment changes. Stop the idle worker before changing its dependencies.
- External terminals and the shell must use the same venv and Hugging Face credential
  paths. Do not embed tokens into terminal scripts or logs. Open a new external terminal
  session with safely quoted paths; never inject commands into an existing session.
- Keep editor/window code in `InteractiveShellWindow`, transport in
  `PythonShellController` / `StructureSnapshot`, and ASE session behavior in
  `src/python/shell/`. The sidebar owns only the Code section and open action.
- `STRUCT_N` is an ordinary ASE Atoms object for a stable document ID. Synchronize
  native geometry edits while idle; preserve persistent variables and calculator
  objects between runs. Originals remain immutable. `studio.add` uses its supplied
  geometry directly; sidebar replication still rebuilds from raw input.
- Use `ASEStructureBridge` for calculation-free conversion. Preserve serializable
  ASE metadata and double-precision scientific positions; rendering continues to
  use float arrays. `precisePosition()` recognizes legacy float-coordinate edits.
  Clone/delete/replicate operations must carry precision and metadata edit history.
- Python and render threads never share mutable arrays. Validate independent
  snapshots, reject stale run/document revisions, and commit only on the UI thread.
  Conflicting geometry edits are locked while a run is active. Image capture defers
  frame application. Inactive documents are updated without selecting them.
  Include the registered `STRUCT_N` name in snapshot errors. Validate every registered
  object before publishing final states: plain ASE/NumPy edits through aliases are
  not tracked, and a failed validation must not publish partial final updates.
- Coalesce previews, apply an active preview only after the prior frame has been
  presented, and always apply the final successful state. Never refit the camera
  for simulation frames. Preserve appearance by stable atom IDs and surviving bonds.
- Stop cooperatively, then restart the process if blocked. Retain the last published
  geometry after errors/stops and force-sync it back into registered Python objects
  before another run. Native revisions do not track rejected Python edits; preserve
  ASE object aliases and calculators while discarding unpublished structure changes.
  Bound console output. On shutdown, stop shell and file
  workers before finalizing the main embedded interpreter.
- Tests cover bridge precision/metadata and real bundled-process execution,
  persistent sessions, multiple structures, EMT relaxation, streaming and Stop.

## Source Tree

```
src/
  render/
    common/           # Shared abstractions (Renderer base, Camera, RenderSettings)
    opengl/           # OpenGL backend (raster + fragment-shader RT)
    vulkan/           # Vulkan backend (future)
    metal/            # Metal backend (macOS primary)
  platform/           # OS-specific glue (future)
    macos/
    windows/
    linux/
  data/               # Shared — Structure (SoA), ElementData, BondList
  python/             # Shared — PythonRuntime, ASEReader
  io/                 # Shared — FileReader, FileReaderRegistry, AsyncFileLoader
  ui/
    components/       # Qt C++ — viewports (per-backend), FileController, StructureModel
    qml/              # QML — Main, HeaderBar, Sidebar, ViewportPanel
  core/               # Application orchestration
resources/
  icons/              # Sidebar section icons and chevrons (SVG assets)
```

## Architecture Details

### Renderer Abstraction
- Abstract `Renderer` base class in `common/Renderer.h` defines the backend-agnostic interface: `initialize()`, `cleanup()`, `resize()`, `setStructure()`, `render()`, etc.
- Each backend implements this interface (e.g., `OpenGLRenderer`, `RayTracingRenderer`)
- `OpenGLViewport::RendererImpl` manages renderer switching via pointer swap
- Shared `Camera` and `RenderSettings` types are used by all backends

### Data Model & Memory Layout
- Structure-of-Arrays (SoA) for positions, velocities, IDs, types, radii
- GPU-friendly compaction and LOD (upload visible subset only)
- Spatial indexing for selection/picking (CPU BVH or GPU picking buffer)

### Rendering Techniques
- **Interactive mode**: Instanced billboard/impostor spheres (ray-sphere in fragment shader)
- **Ray tracing mode**: Procedural primitives or low-poly proxy geometry with normal reconstruction
- **Ray-traced backgrounds**: Accumulate premultiplied foreground color and coverage independently of the background. Composite background color/opacity in the display pass; background edits and transparent export must preserve accumulated samples. Background changes still invalidate cached raster output.
- **Projection modes**: Both perspective and orthographic projection are supported across all renderers. Sphere impostors, RT ray generation, and unit cell occlusion all branch on `isPerspective` at the shader level. The Camera class handles both projection matrices, and `RenderStateHash` includes projection state for RT accumulation reset.
- **Center rotation gizmo**: Use the shared `GizmoOverlay` transform with fixed logical-pixel dimensions and device-pixel scaling in every backend and render mode. Only camera orientation affects the axes; the overlay stays centered and uses its own orthographic projection, independent of scene zoom, FOV, and clipping planes.

### Acceleration Structure Strategy (for trajectories)
- TLAS rebuild per frame (typical for dynamic atoms)
- BLAS strategy based on representation
- Separate handling for:
  - Static geometry (unit cell box, surfaces)
  - Dynamic atoms
  - Dynamic bonds
  - Volumetric surfaces (isosurfaces)

### Denoising & Sampling (for interactive RT)
- Temporal accumulation + reprojection
- Denoiser integration (requires motion vectors)
- Frame history buffer management

## Others...

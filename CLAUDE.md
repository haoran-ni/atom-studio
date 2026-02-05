# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.
Claude can modify this file as appropriate.

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

## Technology Stack

### Language: C++17/20
- Core engine in modern C++
- Embedded Python (pybind11) for file I/O via ASE
- C API for external bindings

### UI Framework: Qt 6
- Qt Widgets/QML for UI components
- Dedicated render surface/widget for Vulkan integration
- Decoupled UI event loop from render loop (explicit frame scheduling)
- Own renderer abstraction layer (beyond Qt's Vulkan helpers)

### Graphics API: Vulkan + Hardware Ray Tracing
- **Raster backend (Vulkan)**: Always-on interactive viewport
- **RT backend A (Vulkan RT)**: Hardware-accelerated ray tracing (Windows/Linux)
  - Acceleration structures (BLAS/TLAS)
  - Ray tracing pipeline and ray queries
- **RT backend B (future)**: Metal RT for macOS or OptiX for NVIDIA optimization
- Compute-shader RT as fallback only

### Platform Strategy
- Windows/Linux: Full Vulkan + Vulkan RT support
- macOS: Vulkan via MoltenVK; RT support TBD (may be raster-only for v1)

### Build System: CMake
- vcpkg or Conan for dependency management
- Cross-platform CI pipeline

### File I/O: Python ASE Integration
- Uses ASE (Atomic Simulation Environment) via embedded Python
- Supports 70+ file formats out of the box (XYZ, CIF, LAMMPS, VASP, PDB, etc.)
- Architecture:
  - Python initialized at startup, ASE pre-loaded
  - Worker threads acquire GIL for file reading
  - ASE reads files → converted to C++ Structure with zero-copy numpy access
  - Bundled Python distribution for deployment (no system Python required)

## Architecture Details

### Data Model & Memory Layout
- Structure-of-Arrays (SoA) for positions, velocities, IDs, types, radii
- GPU-friendly compaction and LOD (upload visible subset only)
- Spatial indexing for selection/picking (CPU BVH or GPU picking buffer)

### Rendering Techniques
- **Interactive mode**: Instanced billboard/impostor spheres (ray-sphere in fragment shader)
- **Ray tracing mode**: Procedural primitives or low-poly proxy geometry with normal reconstruction

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
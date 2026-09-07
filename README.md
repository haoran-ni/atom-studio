# ATOM-STUDIO

A high-performance desktop application for visualization of atomic structures.

## Features

- Interactive visualization panel
- Bright neutral right-hand sidebar with section icons and tunable-parameter hierarchy lines
- Compact color controls with Grid, Spectrum, and RGB/hex tabs, background opacity, and session swatches
- Opaque atoms and bonds, with optional ray-traced shadows and ambient occlusion
- PNG image export with optional transparent background and axes
- Ray-traced images retain accumulated samples when changing background color or opacity, including transparent PNG export
- Export the current edited structure as FHI-aims `.in`, CIF, POSCAR, or extended XYZ
- Real-time ray tracing renderer (planned)
- GPU and CPU optimized
- Supports various atomic structure file formats
- Cross-platform: Windows, macOS, and Linux

## Unit Cell Replication

In **Structure → Replicate Unit Cell**, use the up/down arrows beside X, Y, and Z
to change a replication count by one and apply it immediately. You can also type
a count from 1 to 99 and press **Enter**; leaving a field without pressing Enter
discards that uncommitted value. X, Y, and Z correspond to lattice vectors a, b,
and c, including for non-orthogonal cells.

Replication always rebuilds from the original input cell, replacing working
edits. Counts persist when the sidebar section is closed and return to 1 when
loading a structure or choosing **Reset to Original**. Replication requires a lattice.

## Structure Export

Choose a format from **Files → Export Structures** in the sidebar. Export uses
the current working structure, including deleted atoms and replicated unit cells,
with its current atom types, positions, and lattice. Cartesian coordinates and
lattice lengths use ångströms; atoms are never wrapped into the cell.

- **FHI-aims `.in`**: Cartesian `atom` records and `lattice_vector` records when a lattice exists.
- **`.cif`**: explicit atomic sites with P1 symmetry, cell lengths/angles, and fractional positions.
  Without a lattice, writes Cartesian sites and omits the cell and symmetry records.
- **POSCAR**: lattice vectors and Cartesian positions, grouped by element.
  A structure without a lattice produces an error.
- **`.xyz`**: extended XYZ using `Lattice`, `Properties=species:S:1:pos:R:3`, and `pbc`
  metadata. Without a lattice, omits `Lattice` and writes nonperiodic boundary flags.

Exports run in the background from a snapshot taken when the save path is confirmed.
The destination is replaced only after the complete file has been written successfully.

## Requirements

- CMake 3.25+
- Qt 6.x (Core, Gui, Quick, QuickControls2)
- C++20 compatible compiler
- Vulkan SDK (optional, for future rendering features)

## Building from Source

### macOS (Homebrew)

1. **Install dependencies:**
   ```bash
   brew install qt cmake pybind11 python
   ```
   Apple's Xcode Command Line Tools (or Xcode) must also be installed.

2. **Prepare a dedicated build-time Python environment:**
   ```bash
   python3 -m venv .venv-build
   .venv-build/bin/python -m pip install -r cmake/python-requirements.txt
   ```
   ASE installs NumPy and its other dependencies. No AGL setup script or global
   framework modification is needed.

3. **Build:**
   ```bash
   cmake --preset=default -DPython3_EXECUTABLE="$PWD/.venv-build/bin/python"
   cmake --build build
   ```

   The normal build automatically bundles the selected Python interpreter library,
   standard library, ASE, and their native dependencies. It also validates isolated
   Python startup and structure I/O without using an external Python runtime.

4. **Run:**
   ```bash
   ./build/bin/atom-studio.app/Contents/MacOS/atom-studio
   ```

### Linux

```bash
# Install Qt6 (Ubuntu/Debian)
sudo apt install qt6-base-dev qt6-declarative-dev qt6-quickcontrols2-dev

# Build
cmake --preset=default
cmake --build build

# Bundled Python is synchronized automatically as part of the build

# Run
./build/bin/atom-studio
```

### Windows

```bash
# With vcpkg
cmake --preset=vcpkg-default
cmake --build build-vcpkg

# Bundled Python is synchronized automatically as part of the build

# Run
./build-vcpkg/bin/atom-studio.exe
```

## Creating a Distributable Package

### macOS

To build a Release app and package its Qt dependencies:

```bash
cmake --preset=release -DPython3_EXECUTABLE="$PWD/.venv-build/bin/python"
cmake --build build-release
cmake --build build-release --target deploy
ctest --test-dir build-release --output-on-failure
```

The result is `build-release/bin/atom-studio.app`. A successfully deployed bundle
does not require users to install Python, Conda, ASE, or Qt. Check `macdeployqt`
output for unresolved-framework errors; its exit status alone is not sufficient.
Python is private to the app: it ignores external
Python settings and packages, does not modify the user's Python environment,
and reports a broken bundle instead of falling back to an external interpreter.

Normal builds bundle Python; `deploy` additionally packages Qt. The Python bundle
test checks relocation, conflicting environment settings, native dependencies,
ASE file I/O, and rejection of missing or incompatible runtime files. Python GUI
modules such as Tk are not a supported app interface.

Developer ID signing, notarization, and DMG/PKG creation are not yet automated.
Release architecture and minimum macOS version must also be selected and verified
before public distribution. Python package versions currently follow the selected
build environment; use a controlled environment for release builds.

## Project Structure

```
atom-studio/
├── CMakeLists.txt          # Root CMake configuration
├── vcpkg.json              # vcpkg manifest
├── CMakePresets.json       # CMake presets
├── src/
│   ├── main.cpp            # Application entry point
│   ├── core/               # Core application logic
│   │   ├── Application.h
│   │   └── Application.cpp
│   └── ui/                 # UI components
│       ├── qml/            # QML UI files
│       │   ├── Main.qml
│       │   ├── HeaderBar.qml
│       │   ├── Sidebar.qml
│       │   ├── ViewportPanel.qml
│       │   ├── CollapsibleSection.qml  # Reusable collapsible section
│       │   ├── PropertyRow.qml         # Reusable label/value row
│       │   ├── NumericSliderControl.qml # Reusable slider + text + reset
│       │   ├── ColorPicker.qml         # Compact color row and rainbow button
│       │   ├── ColorPickerPopup.qml    # Shared Grid/Spectrum/Sliders editor
│       │   ├── InfoOverlayBox.qml      # Reusable semi-transparent overlay
│       │   └── AppMenuActions.qml      # Shared File/Edit menu actions
│       └── components/     # C++ UI components
│           ├── VulkanViewport.h
│           └── VulkanViewport.cpp
├── resources/              # Application resources
│   ├── resources.qrc       # QML + SVG resource bundle
│   └── icons/              # Sidebar tab icons and chevron SVG assets
├── cmake/                  # CMake helpers
└── scripts/                # Build/setup scripts
```

## License

[License information here]

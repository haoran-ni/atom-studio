# ATOM-STUDIO

A high-performance desktop application for visualization of atomic structures.

## Features

- Interactive visualization panel with multiple independent structures and a floating structure switcher
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

## Multiple Structures

Import one or more files using **Files → Import Structures**, or drop files onto
the viewport at any time. Imports are queued in order, and each successful import
becomes a new entry, even when importing the same file again. The newest entry is
selected automatically. A failed import leaves existing structures intact and the
remaining queued files continue loading.

When two or more structures are loaded, a floating slider appears at the bottom
of the viewport. Drag it, use its arrow buttons, or focus it and use the keyboard
arrow keys to switch structures. The label shows the structure ID, source filename,
and position in the collection. This control is excluded from exported images.

Each import receives a session ID starting at **0**. Its immutable original and
editable working copy are named `STRUCT_<ID>_RAW` and `STRUCT_<ID>_CURRENT`.
IDs are retained through edits and resets. Positions, atom/bond deletions,
selections, and selected-object colors/sizes belong to that structure.
**Reset to Original** restores only the active structure's working edits, keeping
the shared replication counts and view settings.

All structures share camera orientation, zoom, projection, and pan relative to
their own center, along with renderer mode, lighting, atom scale, default color
scheme, and bond settings. Each structure uses its unit-cell center when a lattice
exists, or its geometric atom center otherwise. Switching or importing recenters
the camera without fitting the zoom. Auto-fit applies to the first structure and
explicit geometry resets, using the largest structure's extent so the shared scale
accommodates the collection. Custom selected-object appearance overrides remain
with their structure.

Only the active structure is prepared and rendered. Inactive structures retain
CPU data for their originals and edits, without a separate viewport or GPU scene.
The previous completed image stays visible until the next structure's first frame
is ready, avoiding a blank viewport during preparation. Ray tracing starts again
from zero on each switch. Structure export saves the active working copy;
image capture briefly defers structure switching
until the requested image has been saved.

## Unit Cell Replication

In **Structure → Replicate All Unit Cells**, use the up/down arrows beside X, Y, and Z
to change a replication count by one and apply it immediately. You can also type
a count from 1 to 99 and press **Enter**; leaving a field without pressing Enter
discards that uncommitted value. X, Y, and Z correspond to lattice vectors a, b,
and c, including for non-orthogonal cells.

Replication counts are global: a change rebuilds every imported structure that
has a lattice from its original input cell, replacing working edits for those
structures. New imports inherit the current counts. Structures without a lattice
retain their working edits and are not replicated. With auto-fit enabled, the
camera is adjusted once after all structures have been updated.

Counts persist when switching views, resetting the active structure, or closing
the sidebar section. Set X, Y, and Z back to 1 to restore single cells globally.

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

## Build and Install on Your Mac (One Command)

Download this repository using **Code → Download ZIP** on GitHub and extract it,
or clone it. Open Terminal in the extracted `atom-studio` folder and run:

```bash
bash scripts/build-macos-dmg.sh
```

When the build finishes, the script opens a disk image. **Drag ATOM-STUDIO into
Applications**, eject the disk image, and launch the app from Applications.
No paid Apple Developer account or notarization is needed to build your own copy.

The automatic setup requires **macOS 14 or newer** and internet access for missing
dependencies. Apple Silicon is the primary supported build platform. The script
also attempts native Intel builds, but Homebrew now gives Intel Macs limited
support and dependencies may need to compile from source. It produces a native
app for the Mac running the script, not a universal Intel/Apple Silicon app.
See [Homebrew's platform requirements](https://docs.brew.sh/Installation).

The script handles the following steps:

- Checks Apple Command Line Tools and opens their installer if needed.
- Installs Homebrew using its official installer if it is missing, then installs
  missing CMake, Python 3.12, and the required Qt modules.
- Creates a private Python environment under `out/macos/venv` and installs ASE,
  NumPy, their dependencies, and the build tools there.
- Builds a Release app in `out/macos/build` and runs the project's tests.
- Bundles Qt, Python, ASE and their native dependencies; checks for missing
  libraries, architecture mismatches and dependencies outside the app.
- Applies free local signatures, briefly launches the packaged app to check
  startup, then creates and verifies the compressed `.dmg`.

**First-run prompts:** approve Apple's installer and finish installing Command
Line Tools, then press Return in Terminal when asked. Homebrew may ask for your
Mac administrator password and confirmation. Run the script as your normal user,
**without `sudo`**. It cannot bypass these installer prompts, an unaccepted Xcode
license, or restrictions on a managed Mac. Full Xcode is not normally required.

Existing prerequisites are reused. Installing a missing Homebrew package can
also install or upgrade its dependencies. The script does not modify your shell
startup files or install Python packages into your system Python or Conda
environment. Homebrew and its installed packages remain available after building.
Allow several GB of free disk space; initial downloads and compilation can take
some time.

The disk image is saved as
`out/macos/ATOM-STUDIO-<version>-macOS-<architecture>.dmg`, with a `.sha256` checksum
beside it. Build output is saved in `out/macos/build.log`, Qt deployment details
in `out/macos/qt-deploy.log`, and selected Python package versions in
`out/macos/python-packages.txt`. The image records the build
Mac's macOS version as its minimum; it does not claim compatibility with older
macOS versions. These files and the build environment are ignored by Git.

To leave the disk image closed or reduce compiler memory use:

```bash
bash scripts/build-macos-dmg.sh --no-open --jobs 4
```

If a step fails, fix the reported problem and rerun the same command. The script
reuses its build environment, refreshes CMake's dependency discovery, and retains
a previous successful disk image until its replacement passes validation. Run
`bash scripts/build-macos-dmg.sh --help` for the available options.

The installed app contains its runtime dependencies: you can remove this source
checkout after copying the app to Applications. If you share the disk image with
others, it is **locally signed, not Apple-notarized**. macOS may block a downloaded
copy until the recipient allows it through **System Settings → Privacy & Security
→ Open Anyway**. See [Apple's instructions](https://support.apple.com/guide/mac-help/mh40616/mac).

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

For a complete locally signed disk image, use the one-command script above.
For manual development or release preparation, build a Release app and package
its Qt dependencies with:

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

The one-command script automates local signing and DMG creation. Developer ID
signing, Apple notarization, and PKG creation are not automated. A release intended
for other Macs still needs testing on its advertised architectures and minimum
macOS versions. Python package versions follow the selected build environment;
use a controlled environment for reproducible public releases.

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

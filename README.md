# ATOM-STUDIO

A high-performance desktop application for visualization of atomic structures.

## Features

- Interactive visualization panel
- Real-time ray tracing renderer (planned)
- GPU and CPU optimized
- Supports various atomic structure file formats
- Cross-platform: Windows, macOS, and Linux

## Requirements

- CMake 3.25+
- Qt 6.x (Core, Gui, Quick, QuickControls2)
- C++20 compatible compiler
- Vulkan SDK (optional, for future rendering features)

## Building from Source

### macOS (Homebrew)

1. **Install dependencies:**
   ```bash
   brew install qt cmake
   ```

2. **Run the setup script** (one-time, creates AGL stub for Qt6):
   ```bash
   chmod +x scripts/setup-macos.sh
   ./scripts/setup-macos.sh
   ```

3. **Build:**
   ```bash
   cmake --preset=system-qt
   cmake --build build
   ```

4. **Run:**
   ```bash
   ./build/bin/atom-studio.app/Contents/MacOS/atom-studio
   ```

### Linux

```bash
# Install Qt6 (Ubuntu/Debian)
sudo apt install qt6-base-dev qt6-declarative-dev qt6-quickcontrols2-dev

# Build
cmake --preset=system-qt
cmake --build build

# Run
./build/bin/atom-studio
```

### Windows

```bash
# With vcpkg
cmake --preset=default
cmake --build build

# Run
./build/bin/atom-studio.exe
```

## Creating a Distributable Package

### macOS

To create a self-contained app bundle with all dependencies:

```bash
cmake --build build --target deploy
```

The resulting `build/bin/atom-studio.app` can be distributed to other macOS users.

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
│       │   └── ViewportPanel.qml
│       └── components/     # C++ UI components
│           ├── VulkanViewport.h
│           └── VulkanViewport.cpp
├── resources/              # Application resources
├── cmake/                  # CMake helpers
└── scripts/                # Build/setup scripts
```

## License

[License information here]

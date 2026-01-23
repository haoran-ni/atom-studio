#!/bin/bash
# setup-macos.sh
# Sets up the development environment for ATOM-STUDIO on macOS
# This script creates the AGL stub framework required by Qt6's OpenGL module

set -e

echo "ATOM-STUDIO macOS Development Setup"
echo "===================================="

# Check if running on macOS
if [[ "$(uname)" != "Darwin" ]]; then
    echo "This script is only needed on macOS."
    exit 0
fi

# Check if Homebrew Qt is installed
if [[ ! -d "/opt/homebrew/lib" ]]; then
    echo "Homebrew lib directory not found at /opt/homebrew/lib"
    echo "If you're using a different Qt installation, you may not need this fix."
    exit 0
fi

# Check if AGL framework already exists
if [[ -d "/opt/homebrew/lib/AGL.framework" ]]; then
    echo "AGL framework stub already exists at /opt/homebrew/lib/AGL.framework"
    echo "No action needed."
    exit 0
fi

echo ""
echo "Qt6's OpenGL module requires the AGL framework, which Apple removed from"
echo "modern macOS. This script creates a stub framework to satisfy this dependency."
echo ""
echo "This requires sudo access to write to /opt/homebrew/lib"
echo ""

read -p "Continue? [y/N] " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Aborted."
    exit 1
fi

echo "Creating AGL stub framework..."

# Create framework directory structure
sudo mkdir -p /opt/homebrew/lib/AGL.framework/Versions/A

# Create and compile stub
TEMP_DIR=$(mktemp -d)
cat > "$TEMP_DIR/agl_stub.c" << 'EOF'
// AGL stub - satisfies Qt6 OpenGL module dependency on modern macOS
// AGL (Apple GL) was deprecated and removed from macOS
void __agl_stub(void) {}
EOF

# Compile the stub
clang -dynamiclib "$TEMP_DIR/agl_stub.c" -o "$TEMP_DIR/AGL" \
    -install_name "@rpath/AGL.framework/Versions/A/AGL"

# Copy to framework location
sudo cp "$TEMP_DIR/AGL" /opt/homebrew/lib/AGL.framework/Versions/A/AGL

# Create symlinks
cd /opt/homebrew/lib/AGL.framework
sudo ln -sf Versions/A/AGL AGL
sudo ln -sf A Versions/Current

# Cleanup
rm -rf "$TEMP_DIR"

echo ""
echo "AGL stub framework created successfully!"
echo "You can now build and run ATOM-STUDIO."
echo ""
echo "To build:"
echo "  cmake --preset=system-qt"
echo "  cmake --build build"
echo ""
echo "To run:"
echo "  ./build/bin/atom-studio.app/Contents/MacOS/atom-studio"

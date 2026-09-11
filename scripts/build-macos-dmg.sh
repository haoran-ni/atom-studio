#!/bin/bash
# Build a self-contained app and a locally signed disk image. Bash 3.2 compatible.
set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: bash scripts/build-macos-dmg.sh [--no-open] [--jobs N]

Install missing build prerequisites, build and test ATOM-STUDIO, then create
and open out/macos/ATOM-STUDIO-<version>-macOS-<architecture>.dmg.

Requires macOS 14+, internet access for missing dependencies, and a normal
user account. Apple/Homebrew installers may request confirmation or an admin
password. Do not run this script with sudo. No Apple Developer account needed.

  --no-open  Leave the finished disk image closed (useful for automation).
  --jobs N   Limit simultaneous compiler jobs (default: up to 8).
  --help     Show this help without installing or building anything.
EOF
}

fail() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }
step() { printf '\n==> %s\n' "$*"; }

open_dmg=1
jobs=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-open) open_dmg=0; shift ;;
        --jobs)
            [[ $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] || fail '--jobs requires a positive integer.'
            jobs="$2"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) fail "Unknown argument: $1. Use --help for usage." ;;
    esac
done

[[ "$(/usr/bin/uname -s)" == Darwin ]] || fail 'This script runs on macOS only.'
[[ "$EUID" -ne 0 ]] || fail 'Run this script as your normal user, without sudo.'
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
[[ "$repo_dir" != *';'* && "$repo_dir" != *$'\n'* ]] || fail 'Use a checkout path without semicolons or newlines (CMake path limitation).'

# Restart natively if Terminal was launched with Rosetta. Never mix Homebrew
# architectures or make the user install Rosetta to build on Apple Silicon.
if [[ "$(/usr/sbin/sysctl -in sysctl.proc_translated 2>/dev/null || true)" == 1 ]]; then
    args=(--jobs "${jobs:-8}")
    [[ "$open_dmg" == 1 ]] || args+=(--no-open)
    exec /usr/bin/arch -arm64 /bin/bash "$repo_dir/scripts/build-macos-dmg.sh" "${args[@]}"
fi
architecture="$(/usr/bin/uname -m)"
case "$architecture" in
    arm64) brew_prefix=/opt/homebrew ;;
    x86_64) brew_prefix=/usr/local ;;
    *) fail "Unsupported architecture: $architecture" ;;
esac
macos_version="$(/usr/bin/sw_vers -productVersion)"
[[ "${macos_version%%.*}" -ge 14 ]] || fail 'Automatic setup requires macOS 14 or newer. See the manual build instructions in README.md.'
if [[ "$architecture" == x86_64 ]]; then
    printf '%s\n' 'Intel build: Homebrew support is limited; some prerequisites may need to compile from source.'
fi

output_dir="$repo_dir/out/macos"
mkdir -p "$output_dir"
# A concurrent run would mutate the same venv, CMake build and disk image.
lock_dir="$output_dir/.build-lock"
mkdir "$lock_dir" 2>/dev/null || fail "Another build may be running. If a previous build was killed, remove $lock_dir and retry."
installer=""
cleanup() {
    [[ -z "$installer" ]] || rm -f -- "$installer"
    rmdir "$lock_dir" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
exec > >(/usr/bin/tee "$output_dir/build.log") 2>&1
trap 'status=$?; printf "\nBuild failed at line %s (exit %s). Fix the error above, then rerun the same command.\nLog: %s/build.log\n" "$LINENO" "$status" "$output_dir" >&2; exit "$status"' ERR

# Select tools explicitly rather than picking up Conda, a virtualenv, or another
# Qt installation. These changes affect this process and its children only.
export PATH="$brew_prefix/bin:$brew_prefix/sbin:/usr/bin:/bin:/usr/sbin:/sbin"
unset PYTHONHOME PYTHONPATH PYTHONUSERBASE PYTHONSTARTUP __PYVENV_LAUNCHER__ VIRTUAL_ENV
unset CMAKE_PREFIX_PATH CMAKE_TOOLCHAIN_FILE CMAKE_GENERATOR CC CXX CPPFLAGS CFLAGS CXXFLAGS LDFLAGS
unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML_IMPORT_PATH QML2_IMPORT_PATH
unset DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH DYLD_FALLBACK_LIBRARY_PATH DYLD_FALLBACK_FRAMEWORK_PATH DYLD_INSERT_LIBRARIES
export HOMEBREW_NO_ANALYTICS=1

step 'Checking Apple Command Line Tools'
tools_ready() {
    /usr/bin/xcrun --find clang++ >/dev/null 2>&1 &&
        /usr/bin/xcrun --sdk macosx --show-sdk-path >/dev/null 2>&1
}
if ! tools_ready; then
    [[ -t 0 ]] || fail 'Install Apple Command Line Tools with xcode-select --install, then rerun in Terminal.'
    /usr/bin/xcode-select --install || true
    while ! tools_ready; do
        printf '%s\n' 'Complete the Apple installer dialog, then press Return to continue (Ctrl-C cancels).'
        read -r _ || fail 'Input closed. Finish installing Command Line Tools and rerun this script.'
    done
fi
# Compilation also detects an unaccepted Xcode license or a broken SDK selection.
printf 'int main() { return 0; }\n' | /usr/bin/xcrun clang++ -x c++ -fsyntax-only -

step 'Checking Homebrew'
brew="$brew_prefix/bin/brew"
if [[ ! -x "$brew" ]]; then
    [[ -t 0 ]] || fail 'Homebrew is missing. Rerun in Terminal so its installer can request administrator access.'
    step 'Installing Homebrew using its official installer'
    installer="$(/usr/bin/mktemp "${TMPDIR:-/tmp}/atom-homebrew.XXXXXX")"
    /usr/bin/curl --fail --show-error --location --retry 3 --proto '=https' --tlsv1.2 \
        https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh -o "$installer"
    /bin/bash "$installer"
    rm -f -- "$installer"
    installer=""
fi
[[ -x "$brew" ]] || fail "Homebrew was not installed at $brew_prefix. Finish its installation and retry."

ensure_formula() {
    local formula="$1" required_file="$2"
    if [[ ! -e "$brew_prefix/opt/$formula/$required_file" ]]; then
        step "Installing $formula"
        "$brew" install "$formula"
    fi
    [[ -e "$brew_prefix/opt/$formula/$required_file" ]] || fail "Homebrew's $formula installation is incomplete. Run: $brew reinstall $formula"
}
step 'Preparing CMake, Qt and Python'
ensure_formula cmake bin/cmake
ensure_formula python@3.12 bin/python3.12
cmake="$brew_prefix/opt/cmake/bin/cmake"
cmake_version="$("$cmake" --version)"
cmake_version="${cmake_version#cmake version }"
cmake_version="${cmake_version%%$'\n'*}"
IFS=. read -r cmake_major cmake_minor _ <<< "$cmake_version"
if (( cmake_major < 3 || (cmake_major == 3 && cmake_minor < 25) )); then
    step 'Updating CMake to meet the minimum version (3.25)'
    "$brew" upgrade cmake
fi

# Reuse the older all-in-one Qt formula when available. Fresh installations
# need only these modules, avoiding Qt WebEngine and other unused components.
if [[ -x "$brew_prefix/opt/qt/bin/macdeployqt" &&
      -f "$brew_prefix/opt/qt/lib/cmake/Qt6Quick/Qt6QuickConfig.cmake" &&
      -f "$brew_prefix/opt/qt/share/qt/plugins/imageformats/libqsvg.dylib" ]]; then
    qt_prefixes="$brew_prefix/opt/qt"
    macdeployqt="$brew_prefix/opt/qt/bin/macdeployqt"
    qml_import_dir="$brew_prefix/opt/qt/share/qt/qml"
else
    ensure_formula qtbase bin/macdeployqt
    ensure_formula qtdeclarative lib/cmake/Qt6Quick/Qt6QuickConfig.cmake
    ensure_formula qtsvg share/qt/plugins/imageformats/libqsvg.dylib
    qt_prefixes="$brew_prefix/opt/qtbase;$brew_prefix/opt/qtdeclarative;$brew_prefix/opt/qtsvg"
    macdeployqt="$brew_prefix/opt/qtbase/bin/macdeployqt"
    qml_import_dir="$brew_prefix/opt/qtdeclarative/share/qt/qml"
fi
if [[ -d "$brew_prefix/opt/qtdeclarative/share/qt/qml" ]]; then
    qml_import_dir="$brew_prefix/opt/qtdeclarative/share/qt/qml"
fi
if [[ -d "$brew_prefix/opt/qtbase/share/qt/plugins" ]]; then
    plugin_args=(--plugin-dir "$brew_prefix/opt/qtbase/share/qt/plugins"
                 --plugin-dir "$brew_prefix/opt/qtsvg/share/qt/plugins")
else
    plugin_args=(--plugin-dir "$brew_prefix/opt/qt/share/qt/plugins")
fi
ctest="$brew_prefix/opt/cmake/bin/ctest"
base_python="$brew_prefix/opt/python@3.12/bin/python3.12"
venv="$output_dir/venv"
python="$venv/bin/python"
python_identity="$output_dir/python-identity.txt"
identity="$("$base_python" -I -c 'import os, sys; print(os.path.realpath(sys.executable)); print(sys.version)')"
if [[ ! -x "$python" || ! -f "$python_identity" || "$(cat "$python_identity")" != "$identity" ]]; then
    step 'Creating the private build-time Python environment'
    # This fixed directory belongs exclusively to this script, never to the
    # user's Python installation or the README's manual .venv-build environment.
    rm -rf -- "$venv"
    "$base_python" -I -m venv "$venv"
    printf '%s\n' "$identity" > "$python_identity"
fi
"$python" -I -m pip --isolated --disable-pip-version-check install --only-binary=:all: \
    -r "$repo_dir/cmake/macos-build-requirements.txt"
"$python" -I -m pip --isolated check
"$python" -I -m pip --isolated freeze > "$output_dir/python-packages.txt"
pybind11_dir="$("$python" -I -m pybind11 --cmakedir)"

if [[ -z "$jobs" ]]; then
    jobs="$(/usr/sbin/sysctl -n hw.logicalcpu)"
    [[ "$jobs" -le 8 ]] || jobs=8
fi
build_dir="$output_dir/build"
step "Building ATOM-STUDIO for $architecture on macOS $macos_version"
# --fresh resets dependency discovery after Homebrew upgrades, while retaining
# object files. This build directory is independent of all development presets.
"$cmake" --fresh -S "$repo_dir" -B "$build_dir" -G 'Unix Makefiles' \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DCMAKE_MAKE_PROGRAM=/usr/bin/make \
    "-DCMAKE_CXX_COMPILER=$(/usr/bin/xcrun --find clang++)" \
    "-DCMAKE_OBJCXX_COMPILER=$(/usr/bin/xcrun --find clang++)" \
    "-DCMAKE_OSX_ARCHITECTURES=$architecture" \
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=$macos_version" \
    "-DCMAKE_OSX_SYSROOT=$(/usr/bin/xcrun --sdk macosx --show-sdk-path)" \
    "-DCMAKE_PREFIX_PATH=$qt_prefixes;$pybind11_dir" \
    "-DPython3_EXECUTABLE=$python" -DPython3_FIND_STRATEGY=LOCATION \
    -DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON
"$cmake" --build "$build_dir" --parallel "$jobs"
step 'Running the application and embedded-runtime tests'
"$ctest" --test-dir "$build_dir" --output-on-failure --timeout 180

step 'Bundling Qt, checking portability, signing locally and creating the disk image'
"$python" -I "$repo_dir/scripts/package_macos.py" \
    --app "$build_dir/bin/atom-studio.app" --output-dir "$output_dir" \
    --macdeployqt "$macdeployqt" --qml-dir "$repo_dir/src/ui/qml" \
    --qml-import-dir "$qml_import_dir" --library-dir "$brew_prefix/lib" \
    "${plugin_args[@]}" \
    --architecture "$architecture" --minimum-macos "$macos_version"
dmg="$(cat "$output_dir/latest-dmg.txt")"
printf '\nReady: %s\nOpen the disk image and drag ATOM-STUDIO into Applications.\n' "$dmg"
if [[ "$open_dmg" == 1 ]]; then
    /usr/bin/open "$dmg" || printf 'Open the disk image above manually.\n'
fi

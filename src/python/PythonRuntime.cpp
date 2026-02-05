#include "PythonRuntime.h"

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

#include <iostream>
#include <filesystem>
#include <cstdlib>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <libgen.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace py = pybind11;
namespace fs = std::filesystem;

namespace atom::python {

namespace {

/**
 * @brief Get the path to the application executable
 */
fs::path getExecutablePath() {
#ifdef __APPLE__
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        return fs::canonical(path);
    }
#elif defined(_WIN32)
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) > 0) {
        return fs::path(path);
    }
#else
    // Linux: read /proc/self/exe
    return fs::canonical("/proc/self/exe");
#endif
    return {};
}

/**
 * @brief Get the application bundle/installation directory
 *
 * On macOS: Returns Contents/ directory of the .app bundle
 * On Windows: Returns directory containing the executable
 * On Linux: Returns directory containing the executable
 */
fs::path getAppDirectory() {
    fs::path exePath = getExecutablePath();
    if (exePath.empty()) return {};

#ifdef __APPLE__
    // Check if we're in an app bundle: .app/Contents/MacOS/executable
    fs::path macosDir = exePath.parent_path();
    if (macosDir.filename() == "MacOS") {
        fs::path contentsDir = macosDir.parent_path();
        if (contentsDir.filename() == "Contents") {
            return contentsDir;
        }
    }
    // Not in a bundle, return executable directory
    return macosDir;
#else
    return exePath.parent_path();
#endif
}

/**
 * @brief Check if bundled Python exists and set environment variables
 *
 * On macOS bundle:
 *   Contents/Resources/python/lib/pythonX.Y (standard library)
 *   Contents/Resources/python/lib/pythonX.Y/site-packages (ASE, NumPy)
 *
 * Returns true if bundled Python was found and configured
 */
bool configureBundledPython() {
    fs::path appDir = getAppDirectory();
    if (appDir.empty()) {
        std::cout << "[PythonRuntime] Could not determine app directory" << std::endl;
        return false;
    }

    std::cout << "[PythonRuntime] App directory: " << appDir << std::endl;

#ifdef __APPLE__
    // Look for bundled Python in Resources/python
    fs::path pythonBase = appDir / "Resources" / "python";
    if (!fs::exists(pythonBase)) {
        std::cout << "[PythonRuntime] Bundled Python not found at: " << pythonBase << std::endl;
        return false;
    }

    // Find Python version directory (python3.X)
    fs::path libDir = pythonBase / "lib";
    fs::path pythonLibDir;
    for (const auto& entry : fs::directory_iterator(libDir)) {
        std::string name = entry.path().filename().string();
        if (name.find("python3") == 0 && entry.is_directory()) {
            pythonLibDir = entry.path();
            break;
        }
    }

    if (pythonLibDir.empty() || !fs::exists(pythonLibDir)) {
        std::cout << "[PythonRuntime] Python lib directory not found in: " << libDir << std::endl;
        return false;
    }

    // Set PYTHONHOME to the python base directory
    std::string pythonHome = pythonBase.string();
    setenv("PYTHONHOME", pythonHome.c_str(), 1);

    // Set PYTHONPATH to include site-packages
    fs::path sitePackages = pythonLibDir / "site-packages";
    std::string pythonPath = pythonLibDir.string();
    if (fs::exists(sitePackages)) {
        pythonPath = sitePackages.string() + ":" + pythonPath;
    }
    setenv("PYTHONPATH", pythonPath.c_str(), 1);

    std::cout << "[PythonRuntime] Using bundled Python" << std::endl;
    std::cout << "[PythonRuntime] PYTHONHOME=" << pythonHome << std::endl;
    std::cout << "[PythonRuntime] PYTHONPATH=" << pythonPath << std::endl;

    return true;

#elif defined(_WIN32)
    // Windows bundled Python structure
    fs::path pythonBase = appDir / "python";
    if (!fs::exists(pythonBase)) {
        std::cout << "[PythonRuntime] Bundled Python not found at: " << pythonBase << std::endl;
        return false;
    }

    // Set PYTHONHOME
    std::string pythonHome = pythonBase.string();
    _putenv_s("PYTHONHOME", pythonHome.c_str());

    // Set PYTHONPATH
    fs::path sitePackages = pythonBase / "Lib" / "site-packages";
    std::string pythonPath = sitePackages.string();
    _putenv_s("PYTHONPATH", pythonPath.c_str());

    std::cout << "[PythonRuntime] Using bundled Python" << std::endl;
    std::cout << "[PythonRuntime] PYTHONHOME=" << pythonHome << std::endl;
    std::cout << "[PythonRuntime] PYTHONPATH=" << pythonPath << std::endl;

    return true;

#else
    // Linux bundled Python structure
    fs::path pythonBase = appDir / "python";
    if (!fs::exists(pythonBase)) {
        std::cout << "[PythonRuntime] Bundled Python not found at: " << pythonBase << std::endl;
        return false;
    }

    // Find Python version directory
    fs::path libDir = pythonBase / "lib";
    fs::path pythonLibDir;
    for (const auto& entry : fs::directory_iterator(libDir)) {
        std::string name = entry.path().filename().string();
        if (name.find("python3") == 0 && entry.is_directory()) {
            pythonLibDir = entry.path();
            break;
        }
    }

    if (pythonLibDir.empty()) {
        return false;
    }

    std::string pythonHome = pythonBase.string();
    setenv("PYTHONHOME", pythonHome.c_str(), 1);

    fs::path sitePackages = pythonLibDir / "site-packages";
    std::string pythonPath = pythonLibDir.string();
    if (fs::exists(sitePackages)) {
        pythonPath = sitePackages.string() + ":" + pythonPath;
    }
    setenv("PYTHONPATH", pythonPath.c_str(), 1);

    std::cout << "[PythonRuntime] Using bundled Python" << std::endl;
    std::cout << "[PythonRuntime] PYTHONHOME=" << pythonHome << std::endl;
    std::cout << "[PythonRuntime] PYTHONPATH=" << pythonPath << std::endl;

    return true;
#endif
}

} // anonymous namespace

PythonRuntime& PythonRuntime::instance() {
    static PythonRuntime runtime;
    return runtime;
}

bool PythonRuntime::initialize() {
    if (m_initialized) {
        return true;
    }

    try {
        // Try to configure bundled Python first
        // If bundled Python exists, use it; otherwise fall back to system Python
        m_usingBundledPython = configureBundledPython();

        if (!m_usingBundledPython) {
            std::cout << "[PythonRuntime] Using system Python" << std::endl;
        }

        // Initialize Python interpreter
        py::initialize_interpreter();

        // Use a scope to ensure all pybind11 objects are destroyed before releasing GIL
        {
            // Import sys to check version and paths
            py::module_ sys = py::module_::import("sys");
            py::object version = sys.attr("version");
            std::cout << "[PythonRuntime] Python " << version.cast<std::string>() << std::endl;

            // Print sys.path for debugging
            py::list syspath = sys.attr("path");
            std::cout << "[PythonRuntime] sys.path:" << std::endl;
            for (size_t i = 0; i < syspath.size(); ++i) {
                std::cout << "  " << syspath[i].cast<std::string>() << std::endl;
            }
        }  // pybind11 objects destroyed here while GIL is still held

        // Save main thread state and release GIL
        // This allows worker threads to acquire the GIL
        m_mainThreadState = PyEval_SaveThread();

        m_initialized = true;
        std::cout << "[PythonRuntime] Initialized successfully" << std::endl;

        // Pre-load ASE modules (acquire GIL temporarily)
        // This avoids delay on first file open
        {
            GILGuard gil;
            try {
                std::cout << "[PythonRuntime] Pre-loading ASE..." << std::endl;

                // Import ase.io which is the main module used for file reading
                // This also imports ase, numpy, and other dependencies
                py::module_ ase_io = py::module_::import("ase.io");

                // Get ASE version for display
                py::module_ ase = py::module_::import("ase");
                std::string version = ase.attr("__version__").cast<std::string>();
                std::cout << "[PythonRuntime] ASE " << version << " loaded successfully" << std::endl;

            } catch (const py::error_already_set& e) {
                if (m_usingBundledPython) {
                    std::cerr << "[PythonRuntime] ERROR: Failed to load ASE from bundled Python: "
                              << e.what() << std::endl;
                    std::cerr << "[PythonRuntime] The application bundle may be corrupted." << std::endl;
                } else {
                    std::cerr << "[PythonRuntime] WARNING: ASE not found. "
                              << "Please install ASE: pip install ase" << std::endl;
                    std::cerr << "[PythonRuntime] Error: " << e.what() << std::endl;
                }
            }
        }

        return true;

    } catch (const py::error_already_set& e) {
        std::cerr << "[PythonRuntime] Python initialization error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[PythonRuntime] Initialization error: " << e.what() << std::endl;
        return false;
    }
}

void PythonRuntime::finalize() {
    if (!m_initialized) {
        return;
    }

    try {
        // Restore main thread state (reacquire GIL)
        if (m_mainThreadState) {
            PyEval_RestoreThread(m_mainThreadState);
            m_mainThreadState = nullptr;
        }

        // Finalize interpreter
        py::finalize_interpreter();

        m_initialized = false;
        std::cout << "[PythonRuntime] Finalized" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[PythonRuntime] Finalization error: " << e.what() << std::endl;
    }
}

std::string PythonRuntime::pythonVersion() const {
    if (!m_initialized) return "";

    GILGuard gil;
    try {
        py::module_ sys = py::module_::import("sys");
        return sys.attr("version").cast<std::string>();
    } catch (...) {
        return "";
    }
}

bool PythonRuntime::isASEAvailable() const {
    if (!m_initialized) return false;

    // Assumes GIL is held by caller
    try {
        py::module_::import("ase");
        return true;
    } catch (const py::error_already_set&) {
        return false;
    }
}

std::string PythonRuntime::aseVersion() const {
    if (!m_initialized) return "";

    // Assumes GIL is held by caller
    try {
        py::module_ ase = py::module_::import("ase");
        return ase.attr("__version__").cast<std::string>();
    } catch (...) {
        return "";
    }
}

std::vector<std::string> PythonRuntime::aseSupportedFormats() const {
    if (!m_initialized) return {};

    // Assumes GIL is held by caller
    try {
        py::module_ ase_io = py::module_::import("ase.io");
        py::module_ ase_io_formats = py::module_::import("ase.io.formats");

        // Get all registered formats
        py::dict all_formats = ase_io_formats.attr("ioformats");

        std::vector<std::string> formats;
        for (auto item : all_formats) {
            formats.push_back(item.first.cast<std::string>());
        }
        return formats;

    } catch (...) {
        return {};
    }
}

// ============================================================================
// GILGuard implementation
// ============================================================================

PythonRuntime::GILGuard::GILGuard() {
    m_state = static_cast<int>(PyGILState_Ensure());
}

PythonRuntime::GILGuard::~GILGuard() {
    PyGILState_Release(static_cast<PyGILState_STATE>(m_state));
}

// ============================================================================
// GILRelease implementation
// ============================================================================

PythonRuntime::GILRelease::GILRelease() {
    m_threadState = PyEval_SaveThread();
}

PythonRuntime::GILRelease::~GILRelease() {
    PyEval_RestoreThread(m_threadState);
}

} // namespace atom::python

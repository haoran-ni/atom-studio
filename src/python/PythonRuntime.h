#pragma once

#include <string>
#include <vector>

// Forward declare Python types to avoid including Python.h in header
struct _ts;  // PyThreadState

namespace atom::python {

/**
 * @brief Manages Python interpreter lifecycle
 *
 * Singleton class that handles Python initialization and finalization.
 * Must be initialized before any Python calls and finalized at app shutdown.
 *
 * Threading model:
 * - Main thread initializes Python and releases the GIL
 * - Worker threads use GILGuard RAII class to acquire GIL
 */
class PythonRuntime {
public:
    /**
     * @brief Get singleton instance
     */
    static PythonRuntime& instance();

    /**
     * @brief Initialize Python interpreter
     *
     * Must be called once at application startup, before Qt event loop.
     * After initialization, the main thread releases the GIL so worker
     * threads can acquire it.
     *
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Finalize Python interpreter
     *
     * Must be called at application shutdown, after Qt event loop ends.
     */
    void finalize();

    /**
     * @brief Check if Python is initialized
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Check if using bundled Python
     *
     * Returns true if the application is using bundled Python from the
     * app bundle/installation directory. macOS requires the bundled runtime;
     * Windows/Linux may fall back to the development Python installation.
     */
    bool isUsingBundledPython() const { return m_usingBundledPython; }

    /**
     * @brief Get Python version string
     */
    std::string pythonVersion() const;

    /**
     * @brief Check if ASE is available
     */
    bool isASEAvailable() const;

    /**
     * @brief Get ASE version string
     */
    std::string aseVersion() const;

    /**
     * @brief Get list of ASE supported file formats
     */
    std::vector<std::string> aseSupportedFormats() const;

    /**
     * @brief RAII guard for acquiring Python GIL
     *
     * Use this in worker threads that need to call Python code.
     * The GIL is automatically released when the guard goes out of scope.
     *
     * Example:
     * @code
     * void workerFunction() {
     *     PythonRuntime::GILGuard gil;
     *     // Now safe to call Python code
     *     py::module_::import("ase");
     * }
     * @endcode
     */
    class GILGuard {
    public:
        GILGuard();
        ~GILGuard();

        // Non-copyable, non-movable
        GILGuard(const GILGuard&) = delete;
        GILGuard& operator=(const GILGuard&) = delete;
        GILGuard(GILGuard&&) = delete;
        GILGuard& operator=(GILGuard&&) = delete;

    private:
        int m_state;  // PyGILState_STATE is an int enum
    };

    /**
     * @brief RAII guard for releasing Python GIL
     *
     * Use this in Python threads that need to release the GIL temporarily
     * (e.g., for I/O operations or calling C++ code that doesn't need Python).
     */
    class GILRelease {
    public:
        GILRelease();
        ~GILRelease();

        GILRelease(const GILRelease&) = delete;
        GILRelease& operator=(const GILRelease&) = delete;

    private:
        _ts* m_threadState;
    };

private:
    PythonRuntime() = default;
    ~PythonRuntime() = default;

    // Non-copyable
    PythonRuntime(const PythonRuntime&) = delete;
    PythonRuntime& operator=(const PythonRuntime&) = delete;

    bool m_initialized = false;
    bool m_usingBundledPython = false;
    _ts* m_mainThreadState = nullptr;
};

} // namespace atom::python

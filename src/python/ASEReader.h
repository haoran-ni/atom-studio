#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace atom::data {
class Structure;
}

namespace atom::python {

/**
 * @brief Progress callback for file reading
 *
 * @param progress Progress value from 0.0 to 1.0
 * @param message Status message
 * @return false to cancel, true to continue
 */
using ProgressCallback = std::function<bool(float progress, std::string_view message)>;

/**
 * @brief Result of a file read operation
 */
struct ReadResult {
    bool success = false;
    std::string errorMessage;
    std::unique_ptr<atom::data::Structure> structure;

    static ReadResult Success(std::unique_ptr<atom::data::Structure> s);
    static ReadResult Error(std::string_view message);
};

/**
 * @brief ASE-based atomic structure file reader
 *
 * Uses Python ASE library to read various atomic structure file formats.
 * Supports all formats that ASE supports, including:
 * - XYZ (standard and extended)
 * - LAMMPS dump and data files
 * - CIF (Crystallographic Information File)
 * - VASP POSCAR/CONTCAR
 * - Quantum ESPRESSO input/output
 * - And many more...
 *
 * Thread safety:
 * - This class is NOT thread-safe
 * - Call from worker threads with GIL acquired (use PythonRuntime::GILGuard)
 */
class ASEReader {
public:
    ASEReader();
    ~ASEReader();

    /**
     * @brief Get format name
     */
    std::string_view formatName() const { return "ASE"; }

    /**
     * @brief Get list of supported file extensions
     *
     * Returns common extensions. ASE supports many more formats
     * that may not have standard extensions.
     */
    std::vector<std::string> supportedExtensions() const;

    /**
     * @brief Check if a file can be read
     * @param path File path
     * @return true if file extension is supported
     */
    bool canRead(std::string_view path) const;

    /**
     * @brief Read atomic structure from file
     *
     * @param path File path
     * @param progress Optional progress callback
     * @return ReadResult containing structure or error
     *
     * @note Requires GIL to be held (use PythonRuntime::GILGuard)
     */
    ReadResult read(std::string_view path, ProgressCallback progress = nullptr);

    /**
     * @brief Read atomic structure from string/buffer
     *
     * @param data File contents as string
     * @param format ASE format name (e.g., "xyz", "cif")
     * @param sourceName Name for the source (for error messages)
     * @param progress Optional progress callback
     * @return ReadResult containing structure or error
     *
     * @note Requires GIL to be held
     */
    ReadResult readFromString(std::string_view data,
                              std::string_view format,
                              std::string_view sourceName = "<buffer>",
                              ProgressCallback progress = nullptr);

    /**
     * @brief Get file dialog filter string for Qt
     *
     * Returns a filter string suitable for QFileDialog, e.g.:
     * "Atomic Structure Files (*.xyz *.cif *.dump);;All Files (*)"
     */
    std::string fileDialogFilter() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace atom::python

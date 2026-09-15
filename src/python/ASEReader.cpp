#include "ASEReader.h"
#include "PythonRuntime.h"
#include "ASEStructureBridge.h"
#include "../data/Structure.h"

#include <pybind11/embed.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace py = pybind11;

namespace atom::python {

// ============================================================================
// ReadResult implementation
// ============================================================================

ReadResult ReadResult::Success(std::unique_ptr<atom::data::Structure> s) {
    ReadResult result;
    result.success = true;
    result.structure = std::move(s);
    return result;
}

ReadResult ReadResult::Error(std::string_view message) {
    ReadResult result;
    result.success = false;
    result.errorMessage = std::string(message);
    return result;
}

// ============================================================================
// ASEReader::Impl
// ============================================================================

class ASEReader::Impl {
public:
    /**
     * @brief Convert ASE Atoms object to Structure
     */
    std::unique_ptr<atom::data::Structure> atomsToStructure(py::object atoms);

    /**
     * @brief Report progress if callback is set
     * @return false if cancelled
     */
    bool reportProgress(const ProgressCallback& callback, float progress, std::string_view message) {
        if (callback) {
            return callback(progress, message);
        }
        return true;
    }
};

std::unique_ptr<atom::data::Structure> ASEReader::Impl::atomsToStructure(py::object atoms) {
    return atom::python::atomsToStructure(atoms);
}

// ============================================================================
// ASEReader implementation
// ============================================================================

ASEReader::ASEReader()
    : m_impl(std::make_unique<Impl>()) {
}

ASEReader::~ASEReader() = default;

std::vector<std::string> ASEReader::supportedExtensions() const {
    // ASE supports 70+ file formats - we don't filter by extension
    // Return common extensions for file dialog convenience only
    return {"*"};
}

bool ASEReader::canRead(std::string_view path) const {
    // Let ASE try to read any file - it will report errors for unsupported formats
    // Just check that the file exists
    return std::filesystem::exists(path);
}

ReadResult ASEReader::read(std::string_view path, ProgressCallback progress) {
    // Note: GIL must be held by caller

    try {
        if (!m_impl->reportProgress(progress, 0.0f, "Importing ASE...")) {
            return ReadResult::Error("Cancelled");
        }

        py::module_ ase_io = py::module_::import("ase.io");

        if (!m_impl->reportProgress(progress, 0.1f, "Reading file with ASE...")) {
            return ReadResult::Error("Cancelled");
        }

        // Read with ASE
        // Use index=-1 to get only the last frame (for trajectory files)
        py::object atoms = ase_io.attr("read")(
            std::string(path),
            py::arg("index") = -1
        );

        if (!m_impl->reportProgress(progress, 0.6f, "Converting to Structure...")) {
            return ReadResult::Error("Cancelled");
        }

        // Convert to Structure
        auto structure = m_impl->atomsToStructure(atoms);

        // Set source path
        structure->setSourcePath(std::string(path));

        // Set name from filename
        std::filesystem::path filepath(path);
        structure->setName(filepath.stem().string());

        if (!m_impl->reportProgress(progress, 0.9f, "Applying colors and radii...")) {
            return ReadResult::Error("Cancelled");
        }

        // Update rendering properties
        structure->updateColorsFromElements();
        structure->updateRadiiFromElements(1.0f, false);

        if (!m_impl->reportProgress(progress, 1.0f, "Done")) {
            return ReadResult::Error("Cancelled");
        }

        return ReadResult::Success(std::move(structure));

    } catch (const py::error_already_set& e) {
        std::string error = "ASE error: ";
        error += e.what();
        return ReadResult::Error(error);
    } catch (const std::exception& e) {
        std::string error = "Error reading file: ";
        error += e.what();
        return ReadResult::Error(error);
    }
}

ReadResult ASEReader::readFromString(std::string_view data,
                                      std::string_view format,
                                      std::string_view sourceName,
                                      ProgressCallback progress) {
    try {
        if (!m_impl->reportProgress(progress, 0.0f, "Importing ASE...")) {
            return ReadResult::Error("Cancelled");
        }

        py::module_ ase_io = py::module_::import("ase.io");
        py::module_ io = py::module_::import("io");

        if (!m_impl->reportProgress(progress, 0.1f, "Parsing with ASE...")) {
            return ReadResult::Error("Cancelled");
        }

        // Create StringIO from data
        py::object string_io = io.attr("StringIO")(std::string(data));

        // Read with ASE
        py::object atoms = ase_io.attr("read")(
            string_io,
            py::arg("format") = std::string(format),
            py::arg("index") = -1
        );

        if (!m_impl->reportProgress(progress, 0.6f, "Converting to Structure...")) {
            return ReadResult::Error("Cancelled");
        }

        auto structure = m_impl->atomsToStructure(atoms);
        structure->setSourcePath(std::string(sourceName));
        structure->setName(std::string(sourceName));

        structure->updateColorsFromElements();
        structure->updateRadiiFromElements(1.0f, false);

        if (!m_impl->reportProgress(progress, 1.0f, "Done")) {
            return ReadResult::Error("Cancelled");
        }

        return ReadResult::Success(std::move(structure));

    } catch (const py::error_already_set& e) {
        std::string error = "ASE error: ";
        error += e.what();
        return ReadResult::Error(error);
    } catch (const std::exception& e) {
        std::string error = "Error parsing data: ";
        error += e.what();
        return ReadResult::Error(error);
    }
}

std::string ASEReader::fileDialogFilter() const {
    // ASE supports 70+ formats - show "All Files" as primary option
    // with common format groups for convenience
    std::ostringstream filter;

    filter << "All Files (*)";
    filter << ";;XYZ Files (*.xyz *.extxyz)";
    filter << ";;CIF Files (*.cif)";
    filter << ";;LAMMPS Files (*.dump *.lammpstrj *.lmp *.data)";
    filter << ";;VASP Files (*.poscar *.contcar *.vasp POSCAR CONTCAR)";
    filter << ";;PDB Files (*.pdb)";
    filter << ";;Gaussian Files (*.com *.gjf *.log *.g09 *.g16)";
    filter << ";;Quantum ESPRESSO (*.in *.out *.pwi *.pwo)";
    filter << ";;ASE Files (*.traj *.json *.db)";

    return filter.str();
}

} // namespace atom::python

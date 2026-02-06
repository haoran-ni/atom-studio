#include "ASEReader.h"
#include "PythonRuntime.h"
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
    auto structure = std::make_unique<atom::data::Structure>();

    // Get number of atoms
    size_t n_atoms = atoms.attr("__len__")().cast<size_t>();
    structure->reserve(n_atoms);

    // Get positions (Nx3 numpy array, dtype=float64)
    py::array_t<double, py::array::c_style> positions =
        atoms.attr("positions").cast<py::array_t<double, py::array::c_style>>();
    auto pos_buf = positions.request();
    double* pos_data = static_cast<double*>(pos_buf.ptr);

    // Get atomic numbers (N numpy array, dtype=int)
    py::array_t<int64_t> numbers = atoms.attr("numbers").cast<py::array_t<int64_t>>();
    auto num_buf = numbers.request();
    int64_t* num_data = static_cast<int64_t*>(num_buf.ptr);

    // Get chemical symbols
    py::list symbols = atoms.attr("get_chemical_symbols")();

    // Add atoms
    for (size_t i = 0; i < n_atoms; ++i) {
        float x = static_cast<float>(pos_data[i * 3 + 0]);
        float y = static_cast<float>(pos_data[i * 3 + 1]);
        float z = static_cast<float>(pos_data[i * 3 + 2]);
        int atomic_num = static_cast<int>(num_data[i]);
        std::string symbol = symbols[i].cast<std::string>();

        structure->addAtom(x, y, z, atomic_num, symbol);
    }

    // Get cell (3x3 numpy array)
    py::object cell_obj = atoms.attr("cell");
    py::array_t<double, py::array::c_style> cell_array =
        cell_obj.attr("array").cast<py::array_t<double, py::array::c_style>>();
    auto cell_buf = cell_array.request();
    double* cell_data = static_cast<double*>(cell_buf.ptr);

    // Check if cell is defined (non-zero volume)
    double volume = cell_obj.attr("volume").cast<double>();
    if (volume > 1e-10) {
        auto& lattice = structure->lattice();
        lattice.defined = true;

        // Copy cell matrix (ASE uses row-major: rows are lattice vectors)
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                lattice.matrix[i][j] = cell_data[i * 3 + j];
            }
        }

        // Get PBC
        py::tuple pbc = atoms.attr("pbc");
        lattice.pbc[0] = pbc[0].cast<bool>();
        lattice.pbc[1] = pbc[1].cast<bool>();
        lattice.pbc[2] = pbc[2].cast<bool>();
    }

    // Try to get velocities (optional)
    try {
        if (py::hasattr(atoms, "get_velocities")) {
            py::object vel_obj = atoms.attr("get_velocities")();
            if (!vel_obj.is_none()) {
                py::array_t<double, py::array::c_style> velocities = vel_obj.cast<py::array_t<double>>();
                auto vel_buf = velocities.request();
                double* vel_data = static_cast<double*>(vel_buf.ptr);

                std::vector<float> vx(n_atoms), vy(n_atoms), vz(n_atoms);
                for (size_t i = 0; i < n_atoms; ++i) {
                    vx[i] = static_cast<float>(vel_data[i * 3 + 0]);
                    vy[i] = static_cast<float>(vel_data[i * 3 + 1]);
                    vz[i] = static_cast<float>(vel_data[i * 3 + 2]);
                }
                structure->setVelocities(std::move(vx), std::move(vy), std::move(vz));
            }
        }
    } catch (...) {
        // Velocities not available, that's OK
    }

    // Try to get forces (optional)
    try {
        if (py::hasattr(atoms, "get_forces")) {
            py::object forces_obj = atoms.attr("get_forces")();
            if (!forces_obj.is_none()) {
                py::array_t<double, py::array::c_style> forces = forces_obj.cast<py::array_t<double>>();
                auto force_buf = forces.request();
                double* force_data = static_cast<double*>(force_buf.ptr);

                std::vector<float> fx(n_atoms), fy(n_atoms), fz(n_atoms);
                for (size_t i = 0; i < n_atoms; ++i) {
                    fx[i] = static_cast<float>(force_data[i * 3 + 0]);
                    fy[i] = static_cast<float>(force_data[i * 3 + 1]);
                    fz[i] = static_cast<float>(force_data[i * 3 + 2]);
                }
                structure->setForces(std::move(fx), std::move(fy), std::move(fz));
            }
        }
    } catch (...) {
        // Forces not available
    }

    // Try to get charges (optional)
    try {
        if (py::hasattr(atoms, "get_charges")) {
            py::object charges_obj = atoms.attr("get_charges")();
            if (!charges_obj.is_none()) {
                py::array_t<double> charges_arr = charges_obj.cast<py::array_t<double>>();
                auto charge_buf = charges_arr.request();
                double* charge_data = static_cast<double*>(charge_buf.ptr);

                std::vector<float> charges(n_atoms);
                for (size_t i = 0; i < n_atoms; ++i) {
                    charges[i] = static_cast<float>(charge_data[i]);
                }
                structure->setCharges(std::move(charges));
            }
        }
    } catch (...) {
        // Charges not available
    }

    // Try to get masses (optional)
    try {
        py::array_t<double> masses_arr = atoms.attr("get_masses")().cast<py::array_t<double>>();
        auto mass_buf = masses_arr.request();
        double* mass_data = static_cast<double*>(mass_buf.ptr);

        std::vector<float> masses(n_atoms);
        for (size_t i = 0; i < n_atoms; ++i) {
            masses[i] = static_cast<float>(mass_data[i]);
        }
        structure->setMasses(std::move(masses));
    } catch (...) {
        // Masses not available (will use element defaults)
    }

    // Try to get energy (optional)
    try {
        if (py::hasattr(atoms, "get_potential_energy")) {
            py::object energy_obj = atoms.attr("get_potential_energy")();
            if (!energy_obj.is_none()) {
                structure->setEnergy(energy_obj.cast<double>());
            }
        }
    } catch (...) {
        // Energy not available
    }

    // Copy info dictionary (metadata)
    try {
        py::dict info = atoms.attr("info");
        for (auto item : info) {
            std::string key = py::str(item.first).cast<std::string>();
            std::string value = py::str(item.second).cast<std::string>();
            structure->setInfo(key, value);
        }
    } catch (...) {
        // Info not available
    }

    return structure;
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

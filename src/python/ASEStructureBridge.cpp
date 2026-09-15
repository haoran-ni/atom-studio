#include "ASEStructureBridge.h"
#include "PythonRuntime.h"
#include "PythonSources.h"
#include "../data/Structure.h"
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <iostream>

namespace py = pybind11;
namespace atom::python {
py::module_ bridgeModule() {
    auto modules = py::module_::import("sys").attr("modules");
    if (!modules.contains("_atom_studio_bridge")) {
        auto module = py::module_::import("types").attr("ModuleType")("_atom_studio_bridge");
        py::exec(bridgeSource, module.attr("__dict__"));
        modules["_atom_studio_bridge"] = module;
    }
    return modules["_atom_studio_bridge"].cast<py::module_>();
}

std::unique_ptr<data::Structure> atomsToStructure(py::handle atoms) {
    const auto snapshot = bridgeModule().attr("pack")(atoms).cast<py::dict>();
    const auto positions = snapshot["positions"].cast<std::vector<std::array<double, 3>>>();
    const auto numbers = snapshot["numbers"].cast<std::vector<int>>();
    const auto ids = snapshot["ids"].cast<std::vector<int64_t>>();
    auto result = std::make_unique<data::Structure>();
    result->reserve(positions.size());
    for (size_t i = 0; i < positions.size(); ++i) {
        const auto& p = positions[i];
        result->addAtom(p[0], p[1], p[2], numbers[i]);
        result->setPrecisePosition(i, p[0], p[1], p[2]);
        result->setAtomId(i, ids[i]);
    }
    result->lattice().matrix = snapshot["cell"].cast<std::array<std::array<double, 3>, 3>>();
    result->lattice().pbc = snapshot["pbc"].cast<std::array<bool, 3>>();
    for (const auto& row : result->lattice().matrix)
        for (double value : row) result->lattice().defined |= value != 0;
    result->setASEPayload(snapshot["ase"].cast<std::string>());
    if (snapshot.contains("energy")) result->setEnergy(snapshot["energy"].cast<double>());
    if (snapshot.contains("forces")) {
        const auto forces = snapshot["forces"].cast<std::vector<std::array<float, 3>>>();
        std::vector<float> x, y, z;
        for (const auto& f : forces) { x.push_back(f[0]); y.push_back(f[1]); z.push_back(f[2]); }
        result->setForces(std::move(x), std::move(y), std::move(z));
    }
    auto masses = atoms.attr("get_masses")().cast<std::vector<float>>();
    result->setMasses(std::move(masses));
    auto velocities = atoms.attr("get_velocities")();
    if (!velocities.is_none()) {
        auto values = velocities.cast<std::vector<std::array<float, 3>>>();
        std::vector<float> x, y, z;
        for (const auto& v : values) { x.push_back(v[0]); y.push_back(v[1]); z.push_back(v[2]); }
        result->setVelocities(std::move(x), std::move(y), std::move(z));
    }
    for (auto item : atoms.attr("info").cast<py::dict>())
        result->setInfo(py::str(item.first), py::str(item.second));
    return result;
}

py::object structureToAtoms(const data::Structure& structure) {
    py::dict snapshot;
    std::vector<std::array<double, 3>> positions;
    std::vector<int> numbers;
    std::vector<int64_t> ids;
    for (size_t i = 0; i < structure.atomCount(); ++i) {
        positions.push_back(structure.precisePosition(i));
        numbers.push_back(structure.atomicNumber(i));
        ids.push_back(structure.atomId(i));
    }
    snapshot["positions"] = positions;
    snapshot["numbers"] = numbers;
    snapshot["ids"] = ids;
    snapshot["cell"] = structure.hasLattice() ? structure.lattice().matrix
                                             : std::array<std::array<double, 3>, 3>{};
    snapshot["pbc"] = structure.lattice().pbc;
    snapshot["ase"] = structure.asePayload();
    py::list edits;
    for (const auto& edit : structure.aseEdits()) {
        py::dict value;
        if (edit.isRepeat) value["repeat"] = edit.repeat;
        else value["indices"] = edit.indices;
        edits.append(value);
    }
    snapshot["edits"] = edits;
    return bridgeModule().attr("unpack")(snapshot);
}

int runInteractiveShellWorker() {
    try {
        PythonRuntime::GILGuard gil;
        bridgeModule();
        py::dict scope;
        scope["__name__"] = "__main__";
        py::exec(shellSource, scope);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
}

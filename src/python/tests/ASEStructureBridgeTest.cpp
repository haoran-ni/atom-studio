#include "ASEStructureBridge.h"
#include "../data/Structure.h"
#include "../data/StructureOperations.h"
#include <pybind11/embed.h>
#include <iostream>
#include <cmath>

namespace py = pybind11;
int main() {
    py::scoped_interpreter interpreter;
    try {
        py::dict scope;
        py::exec(R"PY(
import numpy as np
from ase import Atoms
from ase.constraints import FixAtoms
from ase.calculators.calculator import Calculator
class NeverCalculate(Calculator):
    def calculate(self, *args, **kwargs):
        raise AssertionError('Conversion invoked a calculator')
atoms = Atoms('Cu3', positions=[[0.123456789012345, 0, 0], [1, 0, 0], [2, 0, 0]],
              cell=[[5, 0.2, 0], [0, 5, 0], [0, 0, 5]], pbc=[True, True, False])
atoms.set_constraint(FixAtoms(indices=[0]))
atoms.new_array('labels', np.array([10, 20, 30]))
atoms.info['nested'] = {'value': [1, 2, 3]}
atoms.calc = NeverCalculate()
)PY", scope);
        auto native = atom::python::atomsToStructure(scope["atoms"]);
        if (std::abs(native->precisePosition(0)[0] - 0.123456789012345) > 1e-15)
            throw std::runtime_error("Precision lost on import");
        auto cloned = native->clone();
        cloned->setAtomSelected(1, true);
        cloned->deleteSelectedObjects();
        auto repeated = atom::data::replicateCell(*cloned, 2, 1, 2);
        scope["roundtrip"] = atom::python::structureToAtoms(*repeated);
        py::exec(R"PY(
assert len(roundtrip) == 8
assert roundtrip.arrays['labels'].tolist() == [10, 30] * 4
assert roundtrip.constraints[0].get_indices().tolist() == [0, 4, 2, 6] or set(roundtrip.constraints[0].get_indices()) == {0, 2, 4, 6}
assert roundtrip.info['nested'] == {'value': [1, 2, 3]}
assert roundtrip.pbc.tolist() == [True, True, False]
assert abs(roundtrip.positions[0, 0] - 0.123456789012345) < 1e-15
assert abs(roundtrip.positions[2, 0] - 5.123456789012345) < 1e-14
)PY", scope);
        // Legacy float edits must supersede precision storage, never resurrect old positions.
        cloned->positionsX()[0] = 7.5f;
        scope["edited"] = atom::python::structureToAtoms(*cloned);
        py::exec("assert edited.positions[0, 0] == 7.5", scope);
        py::exec("atoms.cell = [[5,0,0],[0,5,0],[0,0,0]]", scope);
        auto slab = atom::python::atomsToStructure(scope["atoms"]);
        if (!slab->hasLattice() || !slab->lattice().pbc[0]) throw std::runtime_error("Partial cell lost");
        py::exec("atoms.positions[0, 0] = np.nan", scope);
        bool rejected = false;
        try { atom::python::atomsToStructure(scope["atoms"]); }
        catch (const py::error_already_set&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Non-finite positions were accepted");
        std::cout << "PASS: precision, metadata, constraints, deletion, replication, validation, calculation-free conversion\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

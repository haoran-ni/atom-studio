#pragma once
#include <memory>
#include <pybind11/pybind11.h>

namespace atom::data { class Structure; }
namespace atom::python {
// Caller must hold the GIL; no returned Structure retains Python references.
pybind11::module_ bridgeModule();
std::unique_ptr<data::Structure> atomsToStructure(pybind11::handle atoms);
pybind11::object structureToAtoms(const data::Structure& structure);
int runInteractiveShellWorker();
}

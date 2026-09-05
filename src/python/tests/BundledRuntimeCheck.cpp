#include "PythonRuntime.h"

#include <pybind11/embed.h>
#include <cstdlib>
#include <iostream>
#include <string>

namespace py = pybind11;

int main(int argc, char** argv) {
    auto& runtime = atom::python::PythonRuntime::instance();
    if (argc == 2 && std::string(argv[1]) == "--expect-missing-runtime") {
        if (!runtime.initialize()) {
            return 0;
        }
        runtime.finalize();
        return 1;
    }
    const std::string originalHome = std::getenv("PYTHONHOME") ? std::getenv("PYTHONHOME") : "";
    const std::string originalPath = std::getenv("PYTHONPATH") ? std::getenv("PYTHONPATH") : "";
    if (!runtime.initialize()) {
        return 1;
    }
    int result = 0;
    try {
        atom::python::PythonRuntime::GILGuard gil;
        py::exec(R"PY(
import sys, os, pathlib, io, gzip, bz2, lzma, ssl, sqlite3, ctypes, hashlib
import numpy, scipy.linalg, ase.io
from ase import Atoms
root = pathlib.Path(os.environ['ATOM_EXPECTED_PYTHON_HOME']).resolve()
def within(path, directory):
    resolved = pathlib.Path(path).resolve()
    return resolved == directory or directory in resolved.parents
assert sys.flags.isolated and sys.flags.ignore_environment and sys.flags.no_user_site
assert sys.flags.no_site and sys.dont_write_bytecode
assert sys.path and all(path and within(path, root) for path in sys.path), sys.path
assert pathlib.Path(sys.prefix).resolve() == root, sys.prefix
assert 'sitecustomize' not in sys.modules and 'usercustomize' not in sys.modules
for module in tuple(sys.modules.values()):
    path = getattr(module, '__file__', None)
    if path and not path.startswith('<'):
        assert within(path, root), path
assert numpy.allclose(scipy.linalg.solve([[2.0]], [4.0]), [2.0])
for codec in (gzip, bz2, lzma):
    assert codec.decompress(codec.compress(b'ATOM-STUDIO')) == b'ATOM-STUDIO'
assert sqlite3.connect(':memory:').execute('select 42').fetchone()[0] == 42
assert hashlib.sha256(b'atom').hexdigest()
stream = io.StringIO()
ase.io.write(stream, Atoms('H2', positions=[[0, 0, 0], [0, 0, 0.74]]), format='extxyz')
stream.seek(0)
structure = ase.io.read(stream, format='extxyz')
assert len(structure) == 2 and abs(structure.get_distance(0, 1) - 0.74) < 1e-8
# Check libraries actually loaded by dyld, including dynamically imported extensions.
dyld = ctypes.CDLL('/usr/lib/libSystem.B.dylib')
dyld._dyld_image_count.restype = ctypes.c_uint32
dyld._dyld_get_image_name.argtypes = [ctypes.c_uint32]
dyld._dyld_get_image_name.restype = ctypes.c_char_p
contents = root.parent.parent
for index in range(dyld._dyld_image_count()):
    path = dyld._dyld_get_image_name(index).decode()
    assert path.startswith(('/System/Library/', '/usr/lib/')) or within(path, contents), path
print('Bundled Python check passed: isolated imports, native libraries, ASE structure I/O')
)PY");
        if (originalHome != (std::getenv("PYTHONHOME") ? std::getenv("PYTHONHOME") : "") ||
            originalPath != (std::getenv("PYTHONPATH") ? std::getenv("PYTHONPATH") : "")) {
            throw std::runtime_error("Python startup changed the process environment");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        result = 1;
    }
    runtime.finalize();
    return result;
}

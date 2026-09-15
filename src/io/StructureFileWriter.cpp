#include "StructureFileWriter.h"

#include <QLocale>
#include <QObject>
#include <QSaveFile>
#include <QTextStream>
#include <cmath>
#include <limits>

namespace atom::io {

const std::array<StructureFileType, 4>& structureFileTypes() {
    static const std::array<StructureFileType, 4> types{{
        {StructureFileFormat::FhiAims, ".in", QObject::tr("FHI-aims Geometry (*.in)"), ".in"},
        {StructureFileFormat::Cif, ".cif", QObject::tr("CIF Structure (*.cif)"), ".cif"},
        {StructureFileFormat::Poscar, "POSCAR", QObject::tr("VASP Structure (POSCAR CONTCAR *.vasp);;All Files (*)"), ""},
        {StructureFileFormat::ExtendedXyz, ".xyz", QObject::tr("Extended XYZ Structure (*.xyz)"), ".xyz"}
    }};
    return types;
}

const StructureFileType* structureFileType(const QString& id) {
    for (const auto& type : structureFileTypes()) {
        if (type.id == id) return &type;
    }
    return nullptr;
}

StructureExportData StructureExportData::fromStructure(const data::Structure& structure) {
    StructureExportData result;
    result.lattice = structure.lattice();
    const size_t count = structure.atomCount();
    result.positions.reserve(count);
    result.atomicNumbers.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        result.positions.push_back(structure.precisePosition(i));
        result.atomicNumbers.push_back(structure.atomicNumber(i));
    }
    return result;
}

namespace {

QString validate(const StructureExportData& structure, StructureFileFormat format) {
    if (structure.positions.empty()) return QObject::tr("There are no atoms to export.");
    if (structure.positions.size() != structure.atomicNumbers.size()) {
        return QObject::tr("The atom positions and types have different lengths.");
    }
    for (size_t i = 0; i < structure.positions.size(); ++i) {
        const int number = structure.atomicNumbers[i];
        if (number < 0 || number >= data::ElementData::MAX_ELEMENTS) {
            return QObject::tr("Atom %1 has an unsupported atomic type.").arg(i + 1);
        }
        for (double coordinate : structure.positions[i]) {
            if (!std::isfinite(coordinate)) {
                return QObject::tr("Atom %1 has a non-finite position.").arg(i + 1);
            }
        }
    }
    if (!structure.lattice.defined) {
        if (format == StructureFileFormat::Poscar) {
            return QObject::tr("POSCAR export requires a valid lattice. This structure has no lattice.");
        }
    } else {
        for (const auto& vector : structure.lattice.matrix) {
            for (double value : vector) {
                if (!std::isfinite(value)) return QObject::tr("The lattice contains a non-finite value.");
            }
        }
        const double volume = structure.lattice.volume();
        if (!std::isfinite(volume) || volume <= 1e-10) {
            return QObject::tr("The lattice is invalid: its three vectors must span a non-zero volume.");
        }
    }
    return {};
}

QString symbol(int number) {
    const auto text = data::ElementData::byAtomicNumber(number).symbol;
    return QString::fromLatin1(text.data(), static_cast<qsizetype>(text.size()));
}

template <typename T>
void writeVector(QTextStream& out, const std::array<T, 3>& vector) {
    out << vector[0] << ' ' << vector[1] << ' ' << vector[2];
}

void writeAims(QTextStream& out, const StructureExportData& structure) {
    out << "# Exported by ATOM-STUDIO\n";
    if (structure.lattice.defined) {
        for (const auto& vector : structure.lattice.matrix) {
            out << "lattice_vector ";
            writeVector(out, vector);
            out << '\n';
        }
    }
    for (size_t i = 0; i < structure.positions.size(); ++i) {
        out << "atom ";
        writeVector(out, structure.positions[i]);
        out << ' ' << symbol(structure.atomicNumbers[i]) << '\n';
    }
}

void writeXyz(QTextStream& out, const StructureExportData& structure) {
    out << structure.positions.size() << '\n';
    if (structure.lattice.defined) {
        out << "Lattice=\"";
        for (size_t i = 0; i < 3; ++i) {
            if (i) out << ' ';
            writeVector(out, structure.lattice.matrix[i]);
        }
        out << "\" ";
    }
    out << "Properties=species:S:1:pos:R:3 pbc=\"";
    for (size_t i = 0; i < 3; ++i) {
        if (i) out << ' ';
        out << (structure.lattice.defined && structure.lattice.pbc[i] ? 'T' : 'F');
    }
    out << "\"\n";
    for (size_t i = 0; i < structure.positions.size(); ++i) {
        out << symbol(structure.atomicNumbers[i]) << ' ';
        writeVector(out, structure.positions[i]);
        out << '\n';
    }
}

void writePoscar(QTextStream& out, const StructureExportData& structure) {
    // Group by species in first-appearance order, preserving atom order within
    // each species. Each species then has exactly one POSCAR count entry.
    std::array<std::vector<size_t>, data::ElementData::MAX_ELEMENTS> groups;
    std::vector<int> species;
    for (size_t i = 0; i < structure.positions.size(); ++i) {
        const int number = structure.atomicNumbers[i];
        if (groups[number].empty()) species.push_back(number);
        groups[number].push_back(i);
    }
    out << "Exported by ATOM-STUDIO\n1.0\n";
    for (const auto& vector : structure.lattice.matrix) {
        writeVector(out, vector);
        out << '\n';
    }
    for (int number : species) out << symbol(number) << ' ';
    out << '\n';
    for (int number : species) out << groups[number].size() << ' ';
    out << "\nCartesian\n";
    for (int number : species) {
        for (size_t i : groups[number]) {
            writeVector(out, structure.positions[i]);
            out << '\n';
        }
    }
}

void writeCif(QTextStream& out, const StructureExportData& structure) {
    out << "data_atom_studio\n";
    const auto& cell = structure.lattice;
    if (cell.defined) {
        out << "_cell_length_a " << cell.a() << '\n'
            << "_cell_length_b " << cell.b() << '\n'
            << "_cell_length_c " << cell.c() << '\n'
            << "_cell_angle_alpha " << cell.alpha() << '\n'
            << "_cell_angle_beta " << cell.beta() << '\n'
            << "_cell_angle_gamma " << cell.gamma() << '\n'
            << "_space_group_name_H-M_alt 'P 1'\n"
            << "_space_group_IT_number 1\n"
            << "loop_\n_space_group_symop_operation_xyz\n'x, y, z'\n";
    }
    out << "loop_\n_atom_site_type_symbol\n_atom_site_label\n_atom_site_occupancy\n";
    const char* coordinateType = cell.defined ? "fract" : "Cartn";
    for (char axis : {'x', 'y', 'z'}) {
        out << "_atom_site_" << coordinateType << '_' << axis << '\n';
    }
    // Compute the coordinate transform once, not once per atom. CIF stores
    // cell lengths/angles and fractional positions; no symmetry is inferred.
    std::array<std::array<double, 3>, 3> inverse{};
    if (cell.defined) {
        inverse = {{cell.cartesianToFractional(1, 0, 0),
                    cell.cartesianToFractional(0, 1, 0),
                    cell.cartesianToFractional(0, 0, 1)}};
    }
    std::array<size_t, data::ElementData::MAX_ELEMENTS> labels{};
    for (size_t i = 0; i < structure.positions.size(); ++i) {
        const int number = structure.atomicNumbers[i];
        const QString element = symbol(number);
        out << element << ' ' << element << ++labels[number] << " 1 ";
        const auto& position = structure.positions[i];
        if (cell.defined) {
            std::array<double, 3> fractional{};
            for (size_t d = 0; d < 3; ++d) {
                for (size_t j = 0; j < 3; ++j) fractional[d] += position[j] * inverse[j][d];
            }
            writeVector(out, fractional);
        } else {
            writeVector(out, position);
        }
        out << '\n';
    }
}

} // namespace

StructureWriteResult writeStructureFile(const QString& path, StructureFileFormat format,
                                       const StructureExportData& structure) {
    if (const QString error = validate(structure, format); !error.isEmpty()) return {false, error};

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return {false, file.errorString()};
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setLocale(QLocale::c());
    out.setRealNumberPrecision(std::numeric_limits<double>::max_digits10);
    switch (format) {
    case StructureFileFormat::FhiAims: writeAims(out, structure); break;
    case StructureFileFormat::Cif: writeCif(out, structure); break;
    case StructureFileFormat::Poscar: writePoscar(out, structure); break;
    case StructureFileFormat::ExtendedXyz: writeXyz(out, structure); break;
    default: return {false, QObject::tr("Unsupported structure export format.")};
    }
    out.flush();
    if (out.status() != QTextStream::Ok) {
        file.cancelWriting();
        return {false, QObject::tr("Could not write the structure file: %1").arg(file.errorString())};
    }
    if (!file.commit()) return {false, file.errorString()};
    return {true, {}};
}

} // namespace atom::io

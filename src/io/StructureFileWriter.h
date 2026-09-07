#pragma once

#include "../data/Structure.h"
#include <QString>
#include <array>
#include <vector>

namespace atom::io {

enum class StructureFileFormat { FhiAims, Cif, Poscar, ExtendedXyz };

struct StructureFileType {
    StructureFileFormat format;
    QString id;
    QString filter;
    QString suffix;
};

const std::array<StructureFileType, 4>& structureFileTypes();
const StructureFileType* structureFileType(const QString& id);

// Independent geometry snapshot: no bonds, rendering buffers, or mutable model
// references are retained while a background export is running.
struct StructureExportData {
    std::vector<std::array<float, 3>> positions;
    std::vector<int> atomicNumbers;
    data::Lattice lattice;

    static StructureExportData fromStructure(const data::Structure& structure);
};

struct StructureWriteResult {
    bool success = false;
    QString error;
};

// Writes a single structure, atomically replacing the destination only after
// serialization succeeds. Cartesian coordinates and cell lengths use Angstroms;
// atomic positions are never wrapped into the cell.
StructureWriteResult writeStructureFile(const QString& path,
                                       StructureFileFormat format,
                                       const StructureExportData& structure);

} // namespace atom::io

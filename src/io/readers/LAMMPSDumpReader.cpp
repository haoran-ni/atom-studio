#include "LAMMPSDumpReader.h"
#include "../../data/AtomicStructure.h"
#include "../../data/UnitCell.h"
#include "../../data/ElementData.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <cmath>

namespace atom::io {

ReadResult LAMMPSDumpReader::read(std::string_view path,
                                   ProgressCallback progress) const {
    std::string contents = readFileContents(path);
    if (contents.empty()) {
        return ReadResult::Error("Could not read file: " + std::string(path));
    }

    auto result = parseDump(contents, path, progress);
    if (result.success && result.structure) {
        result.structure->setSourceFile(std::string(path));
    }
    return result;
}

ReadResult LAMMPSDumpReader::readFromString(std::string_view data,
                                             std::string_view sourceName,
                                             ProgressCallback progress) const {
    return parseDump(data, sourceName, progress);
}

LAMMPSDumpReader::ColumnInfo LAMMPSDumpReader::parseAtomHeader(std::string_view header) {
    ColumnInfo info;

    // Skip "ITEM: ATOMS " prefix
    auto pos = header.find("ATOMS");
    if (pos == std::string_view::npos) return info;
    header = header.substr(pos + 5);

    // Parse column names
    std::istringstream stream{std::string{header}};
    std::string colName;
    int colIndex = 0;

    while (stream >> colName) {
        // Convert to lowercase
        std::transform(colName.begin(), colName.end(), colName.begin(), ::tolower);

        if (colName == "id") info.idCol = colIndex;
        else if (colName == "type") info.typeCol = colIndex;
        else if (colName == "element") info.elementCol = colIndex;
        else if (colName == "x") info.xCol = colIndex;
        else if (colName == "y") info.yCol = colIndex;
        else if (colName == "z") info.zCol = colIndex;
        else if (colName == "xs") info.xsCol = colIndex;
        else if (colName == "ys") info.ysCol = colIndex;
        else if (colName == "zs") info.zsCol = colIndex;
        else if (colName == "xu") info.xCol = colIndex;  // unwrapped coords
        else if (colName == "yu") info.yCol = colIndex;
        else if (colName == "zu") info.zCol = colIndex;

        ++colIndex;
    }

    // Determine if we should use scaled coordinates
    if (info.xCol < 0 && info.xsCol >= 0) {
        info.useScaled = true;
    }

    return info;
}

int LAMMPSDumpReader::mapType(int lammpsType) const {
    auto it = m_typeMap.find(lammpsType);
    if (it != m_typeMap.end()) {
        return it->second;
    }
    // Default mapping: just use the type as a crude approximation
    // Common convention: 1=C, 2=H, 3=O, 4=N, etc.
    // Without explicit mapping, use type directly (may be wrong)
    return std::min(lammpsType, data::ElementData::MAX_ELEMENTS - 1);
}

ReadResult LAMMPSDumpReader::parseDump(std::string_view data,
                                        std::string_view sourceName,
                                        ProgressCallback progress) const {
    std::istringstream stream{std::string{data}};
    std::string line;

    auto structure = std::make_unique<data::AtomicStructure>();
    int atomCount = 0;
    float boxLo[3] = {0}, boxHi[3] = {0};
    ColumnInfo columns;
    bool inAtoms = false;
    int atomsRead = 0;

    if (!reportProgress(progress, 0.0f, "Parsing LAMMPS dump...")) {
        return ReadResult::Error("Cancelled");
    }

    while (std::getline(stream, line)) {
        // Trim whitespace
        while (!line.empty() && std::isspace(line.back())) line.pop_back();
        if (line.empty()) continue;

        if (line.find("ITEM:") != std::string::npos) {
            inAtoms = false;

            if (line.find("NUMBER OF ATOMS") != std::string::npos) {
                if (std::getline(stream, line)) {
                    std::from_chars(line.data(), line.data() + line.size(), atomCount);
                    structure->reserve(atomCount);
                }
            }
            else if (line.find("BOX BOUNDS") != std::string::npos) {
                for (int i = 0; i < 3 && std::getline(stream, line); ++i) {
                    std::istringstream box(line);
                    box >> boxLo[i] >> boxHi[i];
                }
                // Set up unit cell
                float a = boxHi[0] - boxLo[0];
                float b = boxHi[1] - boxLo[1];
                float c = boxHi[2] - boxLo[2];
                if (a > 0 && b > 0 && c > 0) {
                    structure->unitCell().setParameters(a, b, c, 90, 90, 90);
                }
            }
            else if (line.find("ATOMS") != std::string::npos) {
                columns = parseAtomHeader(line);
                inAtoms = true;

                if ((columns.xCol < 0 && columns.xsCol < 0) ||
                    (columns.yCol < 0 && columns.ysCol < 0) ||
                    (columns.zCol < 0 && columns.zsCol < 0)) {
                    return ReadResult::Error("Could not find coordinate columns in ATOMS header");
                }
            }
        }
        else if (inAtoms) {
            // Parse atom line
            std::istringstream atomLine(line);
            std::vector<std::string> cols;
            std::string col;
            while (atomLine >> col) {
                cols.push_back(col);
            }

            // Get coordinates
            float x = 0, y = 0, z = 0;
            if (columns.useScaled) {
                float xs = 0, ys = 0, zs = 0;
                if (columns.xsCol >= 0 && columns.xsCol < (int)cols.size())
                    xs = std::stof(cols[columns.xsCol]);
                if (columns.ysCol >= 0 && columns.ysCol < (int)cols.size())
                    ys = std::stof(cols[columns.ysCol]);
                if (columns.zsCol >= 0 && columns.zsCol < (int)cols.size())
                    zs = std::stof(cols[columns.zsCol]);
                // Convert scaled to Cartesian
                x = boxLo[0] + xs * (boxHi[0] - boxLo[0]);
                y = boxLo[1] + ys * (boxHi[1] - boxLo[1]);
                z = boxLo[2] + zs * (boxHi[2] - boxLo[2]);
            } else {
                if (columns.xCol >= 0 && columns.xCol < (int)cols.size())
                    x = std::stof(cols[columns.xCol]);
                if (columns.yCol >= 0 && columns.yCol < (int)cols.size())
                    y = std::stof(cols[columns.yCol]);
                if (columns.zCol >= 0 && columns.zCol < (int)cols.size())
                    z = std::stof(cols[columns.zCol]);
            }

            // Get type
            int atomicNumber = 6; // Default to carbon
            if (columns.elementCol >= 0 && columns.elementCol < (int)cols.size()) {
                atomicNumber = data::ElementData::atomicNumberFromSymbol(cols[columns.elementCol]);
            } else if (columns.typeCol >= 0 && columns.typeCol < (int)cols.size()) {
                int lammpsType = std::stoi(cols[columns.typeCol]);
                atomicNumber = mapType(lammpsType);
            }

            structure->addAtom(x, y, z, atomicNumber);
            ++atomsRead;

            // Report progress
            if (atomsRead % 5000 == 0 && atomCount > 0) {
                float prog = static_cast<float>(atomsRead) / atomCount;
                if (!reportProgress(progress, prog,
                                   "Reading atoms... " + std::to_string(atomsRead) +
                                   "/" + std::to_string(atomCount))) {
                    return ReadResult::Error("Cancelled");
                }
            }
        }
    }

    if (structure->atomCount() == 0) {
        return ReadResult::Error("No atoms found in LAMMPS dump file");
    }

    // Apply CPK colors and radii
    structure->updateColorsFromTypes();
    structure->updateRadiiFromTypes(0.5f);

    structure->setName("LAMMPS Structure (" + std::to_string(structure->atomCount()) + " atoms)");

    reportProgress(progress, 1.0f, "Complete");

    return ReadResult::Success(std::move(structure));
}

} // namespace atom::io

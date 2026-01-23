#pragma once

#include "../FileReader.h"
#include <unordered_map>

namespace atom::io {

/**
 * @brief Reader for LAMMPS dump format files
 *
 * Supports LAMMPS dump file format with custom atom style:
 *   ITEM: TIMESTEP
 *   0
 *   ITEM: NUMBER OF ATOMS
 *   1000
 *   ITEM: BOX BOUNDS pp pp pp
 *   0 10
 *   0 10
 *   0 10
 *   ITEM: ATOMS id type x y z ...
 *   1 1 0.5 0.5 0.5
 *   ...
 *
 * Automatically detects column order from header.
 * Supports scaled (xs, ys, zs) and unscaled (x, y, z) coordinates.
 */
class LAMMPSDumpReader : public FileReader {
public:
    /**
     * @brief Type mapping from LAMMPS type to atomic number
     */
    using TypeMap = std::unordered_map<int, int>;

    /**
     * @brief Set type mapping
     * @param map Map from LAMMPS type (1-based) to atomic number
     */
    void setTypeMapping(const TypeMap& map) { m_typeMap = map; }

    std::string_view formatName() const override { return "LAMMPS Dump"; }

    std::vector<std::string> supportedExtensions() const override {
        return {"dump", "lammpstrj", "lmp"};
    }

    ReadResult read(std::string_view path,
                    ProgressCallback progress = nullptr) const override;

    ReadResult readFromString(std::string_view data,
                              std::string_view sourceName = "buffer",
                              ProgressCallback progress = nullptr) const override;

private:
    struct ColumnInfo {
        int idCol = -1;
        int typeCol = -1;
        int xCol = -1, yCol = -1, zCol = -1;
        int xsCol = -1, ysCol = -1, zsCol = -1;  // scaled coordinates
        int elementCol = -1;  // optional element symbol
        bool useScaled = false;
    };

    ReadResult parseDump(std::string_view data, std::string_view sourceName,
                         ProgressCallback progress) const;

    static ColumnInfo parseAtomHeader(std::string_view header);
    int mapType(int lammpsType) const;

    TypeMap m_typeMap;
};

} // namespace atom::io

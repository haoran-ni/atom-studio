#pragma once

#include "../FileReader.h"

namespace atom::io {

/**
 * @brief Reader for CIF (Crystallographic Information File) format
 *
 * Supports basic CIF format for crystal structures:
 * - Cell parameters (_cell_length_a, _cell_angle_alpha, etc.)
 * - Atom sites (_atom_site_label, _atom_site_fract_x, etc.)
 * - Symmetry operations (_symmetry_equiv_pos_as_xyz)
 *
 * Note: This is a basic implementation. For full CIF support,
 * consider using the gemmi library.
 */
class CIFReader : public FileReader {
public:
    std::string_view formatName() const override { return "CIF"; }

    std::vector<std::string> supportedExtensions() const override {
        return {"cif"};
    }

    ReadResult read(std::string_view path,
                    ProgressCallback progress = nullptr) const override;

    ReadResult readFromString(std::string_view data,
                              std::string_view sourceName = "buffer",
                              ProgressCallback progress = nullptr) const override;

private:
    struct CellParams {
        float a = 0, b = 0, c = 0;
        float alpha = 90, beta = 90, gamma = 90;
        bool valid() const { return a > 0 && b > 0 && c > 0; }
    };

    struct AtomSite {
        std::string label;
        std::string symbol;
        float x = 0, y = 0, z = 0;
        float occupancy = 1.0f;
    };

    struct SymOp {
        // Symmetry operation as 3x4 matrix (rotation + translation)
        float mat[12] = {1,0,0,0, 0,1,0,0, 0,0,1,0};
    };

    ReadResult parseCIF(std::string_view data, std::string_view sourceName,
                        ProgressCallback progress) const;

    static CellParams parseCellParams(const std::vector<std::string>& lines);
    static std::vector<AtomSite> parseAtomSites(const std::vector<std::string>& lines);
    static std::vector<SymOp> parseSymmetryOps(const std::vector<std::string>& lines);
    static SymOp parseSymOpString(std::string_view opStr);
    static std::string extractElement(const std::string& label);
    static std::string getValue(const std::vector<std::string>& lines,
                                const std::string& key);
};

} // namespace atom::io

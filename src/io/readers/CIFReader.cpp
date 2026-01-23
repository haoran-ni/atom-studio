#include "CIFReader.h"
#include "../../data/AtomicStructure.h"
#include "../../data/UnitCell.h"
#include "../../data/ElementData.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <sstream>

namespace atom::io {

ReadResult CIFReader::read(std::string_view path, ProgressCallback progress) const {
    std::string contents = readFileContents(path);
    if (contents.empty()) {
        return ReadResult::Error("Could not read file: " + std::string(path));
    }

    auto result = parseCIF(contents, path, progress);
    if (result.success && result.structure) {
        result.structure->setSourceFile(std::string(path));
    }
    return result;
}

ReadResult CIFReader::readFromString(std::string_view data,
                                      std::string_view sourceName,
                                      ProgressCallback progress) const {
    return parseCIF(data, sourceName, progress);
}

std::string CIFReader::getValue(const std::vector<std::string>& lines,
                                 const std::string& key) {
    for (const auto& line : lines) {
        if (line.find(key) == 0) {
            auto pos = line.find_first_not_of(" \t", key.length());
            if (pos != std::string::npos) {
                std::string value = line.substr(pos);
                // Remove quotes if present
                if ((value.front() == '\'' && value.back() == '\'') ||
                    (value.front() == '"' && value.back() == '"')) {
                    value = value.substr(1, value.length() - 2);
                }
                return value;
            }
        }
    }
    return "";
}

CIFReader::CellParams CIFReader::parseCellParams(const std::vector<std::string>& lines) {
    CellParams params;

    auto parseFloat = [](const std::string& s) -> float {
        if (s.empty() || s == "?" || s == ".") return 0;
        // Remove uncertainty in parentheses, e.g., "5.431(2)" -> "5.431"
        std::string clean = s;
        auto pos = clean.find('(');
        if (pos != std::string::npos) {
            clean = clean.substr(0, pos);
        }
        try {
            return std::stof(clean);
        } catch (...) {
            return 0;
        }
    };

    params.a = parseFloat(getValue(lines, "_cell_length_a"));
    params.b = parseFloat(getValue(lines, "_cell_length_b"));
    params.c = parseFloat(getValue(lines, "_cell_length_c"));
    params.alpha = parseFloat(getValue(lines, "_cell_angle_alpha"));
    params.beta = parseFloat(getValue(lines, "_cell_angle_beta"));
    params.gamma = parseFloat(getValue(lines, "_cell_angle_gamma"));

    // Default angles to 90 if not specified
    if (params.alpha == 0) params.alpha = 90;
    if (params.beta == 0) params.beta = 90;
    if (params.gamma == 0) params.gamma = 90;

    return params;
}

std::string CIFReader::extractElement(const std::string& label) {
    // Extract element symbol from atom label
    // e.g., "Fe1", "O2", "Si3a" -> "Fe", "O", "Si"
    std::string elem;
    for (char c : label) {
        if (std::isalpha(c)) {
            elem += c;
            if (elem.length() == 2) break;
        } else if (!elem.empty()) {
            break;
        }
    }

    // Capitalize first letter, lowercase rest
    if (!elem.empty()) {
        elem[0] = std::toupper(elem[0]);
        for (size_t i = 1; i < elem.length(); ++i) {
            elem[i] = std::tolower(elem[i]);
        }
    }

    return elem;
}

std::vector<CIFReader::AtomSite> CIFReader::parseAtomSites(
    const std::vector<std::string>& lines) {

    std::vector<AtomSite> sites;

    // Find loop_ with _atom_site
    bool inLoop = false;
    std::vector<std::string> columns;
    int labelCol = -1, symbolCol = -1;
    int xCol = -1, yCol = -1, zCol = -1;
    int occCol = -1;

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];

        if (line.find("loop_") == 0) {
            inLoop = true;
            columns.clear();
            labelCol = symbolCol = xCol = yCol = zCol = occCol = -1;
            continue;
        }

        if (inLoop && line.find("_atom_site") == 0) {
            // This is a column header
            std::string colName = line;
            // Remove leading/trailing whitespace
            while (!colName.empty() && std::isspace(colName.back())) colName.pop_back();

            if (colName == "_atom_site_label") labelCol = columns.size();
            else if (colName == "_atom_site_type_symbol") symbolCol = columns.size();
            else if (colName == "_atom_site_fract_x") xCol = columns.size();
            else if (colName == "_atom_site_fract_y") yCol = columns.size();
            else if (colName == "_atom_site_fract_z") zCol = columns.size();
            else if (colName == "_atom_site_occupancy") occCol = columns.size();

            columns.push_back(colName);
            continue;
        }

        if (inLoop && !columns.empty() && !line.empty() && line[0] != '_') {
            // Check if this is a new loop or data
            if (line.find("loop_") == 0 || line.find("data_") == 0) {
                inLoop = false;
                continue;
            }

            // Parse data line
            if (xCol < 0 || yCol < 0 || zCol < 0) continue;

            std::istringstream ss(line);
            std::vector<std::string> values;
            std::string val;
            while (ss >> val) {
                values.push_back(val);
            }

            if (values.size() < columns.size()) continue;

            AtomSite site;

            auto parseCoord = [](const std::string& s) -> float {
                if (s.empty() || s == "?" || s == ".") return 0;
                std::string clean = s;
                auto pos = clean.find('(');
                if (pos != std::string::npos) clean = clean.substr(0, pos);
                try { return std::stof(clean); } catch (...) { return 0; }
            };

            if (labelCol >= 0 && labelCol < (int)values.size())
                site.label = values[labelCol];
            if (symbolCol >= 0 && symbolCol < (int)values.size())
                site.symbol = values[symbolCol];
            site.x = parseCoord(values[xCol]);
            site.y = parseCoord(values[yCol]);
            site.z = parseCoord(values[zCol]);
            if (occCol >= 0 && occCol < (int)values.size())
                site.occupancy = parseCoord(values[occCol]);
            else
                site.occupancy = 1.0f;

            // Get element from symbol or label
            if (site.symbol.empty() && !site.label.empty()) {
                site.symbol = extractElement(site.label);
            }

            sites.push_back(site);
        }
    }

    return sites;
}

CIFReader::SymOp CIFReader::parseSymOpString(std::string_view opStr) {
    SymOp op;
    // Initialize to identity
    std::fill(std::begin(op.mat), std::end(op.mat), 0.0f);

    // Parse symmetry operation string like "x,y,z" or "-x+1/2,y,-z+1/2"
    std::string str(opStr);
    // Remove spaces
    str.erase(std::remove_if(str.begin(), str.end(), ::isspace), str.end());

    // Split by comma
    std::vector<std::string> parts;
    std::istringstream ss(str);
    std::string part;
    while (std::getline(ss, part, ',')) {
        parts.push_back(part);
    }

    if (parts.size() != 3) return op;

    auto parsePart = [](const std::string& s, float* row) {
        // row[0-2] = coefficients for x,y,z, row[3] = translation
        std::fill(row, row + 4, 0.0f);

        std::string term;
        float sign = 1.0f;
        size_t i = 0;

        while (i <= s.length()) {
            char c = (i < s.length()) ? std::tolower(s[i]) : '+';

            if (c == '+' || c == '-' || i == s.length()) {
                // Process accumulated term
                if (!term.empty()) {
                    if (term == "x") row[0] = sign;
                    else if (term == "y") row[1] = sign;
                    else if (term == "z") row[2] = sign;
                    else {
                        // Parse fraction or number
                        float val = 0;
                        auto slashPos = term.find('/');
                        if (slashPos != std::string::npos) {
                            float num = std::stof(term.substr(0, slashPos));
                            float den = std::stof(term.substr(slashPos + 1));
                            val = num / den;
                        } else {
                            try { val = std::stof(term); } catch (...) {}
                        }
                        row[3] += sign * val;
                    }
                    term.clear();
                }
                sign = (c == '-') ? -1.0f : 1.0f;
            } else {
                term += c;
            }
            ++i;
        }
    };

    parsePart(parts[0], &op.mat[0]);  // Row 0: x' coefficients
    parsePart(parts[1], &op.mat[4]);  // Row 1: y' coefficients
    parsePart(parts[2], &op.mat[8]);  // Row 2: z' coefficients

    return op;
}

std::vector<CIFReader::SymOp> CIFReader::parseSymmetryOps(
    const std::vector<std::string>& lines) {

    std::vector<SymOp> ops;

    // Find symmetry operations
    bool inLoop = false;
    int opCol = -1;
    std::vector<std::string> columns;

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];

        if (line.find("loop_") == 0) {
            inLoop = true;
            columns.clear();
            opCol = -1;
            continue;
        }

        if (inLoop && (line.find("_symmetry_equiv_pos_as_xyz") == 0 ||
                       line.find("_space_group_symop_operation_xyz") == 0)) {
            opCol = columns.size();
            columns.push_back(line);
            continue;
        }

        if (inLoop && line.find("_symmetry") == 0) {
            columns.push_back(line);
            continue;
        }

        if (inLoop && opCol >= 0 && !line.empty() && line[0] != '_') {
            if (line.find("loop_") == 0 || line.find("data_") == 0) {
                inLoop = false;
                continue;
            }

            // Extract the symmetry operation string
            std::string opStr;
            if (columns.size() == 1) {
                opStr = line;
            } else {
                std::istringstream ss(line);
                std::vector<std::string> values;
                std::string val;
                while (ss >> val) values.push_back(val);
                if (opCol < (int)values.size()) opStr = values[opCol];
            }

            // Remove quotes
            if (!opStr.empty() && (opStr.front() == '\'' || opStr.front() == '"')) {
                opStr = opStr.substr(1);
            }
            if (!opStr.empty() && (opStr.back() == '\'' || opStr.back() == '"')) {
                opStr.pop_back();
            }

            if (!opStr.empty()) {
                ops.push_back(parseSymOpString(opStr));
            }
        }
    }

    // If no symmetry operations found, add identity
    if (ops.empty()) {
        SymOp identity;
        identity.mat[0] = identity.mat[5] = identity.mat[10] = 1.0f;
        ops.push_back(identity);
    }

    return ops;
}

ReadResult CIFReader::parseCIF(std::string_view data, std::string_view sourceName,
                                ProgressCallback progress) const {
    if (!reportProgress(progress, 0.0f, "Parsing CIF file...")) {
        return ReadResult::Error("Cancelled");
    }

    // Split into lines
    std::vector<std::string> lines;
    std::istringstream stream{std::string{data}};
    std::string line;
    while (std::getline(stream, line)) {
        // Trim trailing whitespace
        while (!line.empty() && std::isspace(line.back())) line.pop_back();
        lines.push_back(line);
    }

    // Parse cell parameters
    CellParams cell = parseCellParams(lines);
    if (!cell.valid()) {
        return ReadResult::Error("Could not parse unit cell parameters");
    }

    if (!reportProgress(progress, 0.2f, "Parsing atom sites...")) {
        return ReadResult::Error("Cancelled");
    }

    // Parse atom sites
    std::vector<AtomSite> sites = parseAtomSites(lines);
    if (sites.empty()) {
        return ReadResult::Error("No atom sites found in CIF file");
    }

    if (!reportProgress(progress, 0.4f, "Parsing symmetry operations...")) {
        return ReadResult::Error("Cancelled");
    }

    // Parse symmetry operations
    std::vector<SymOp> symOps = parseSymmetryOps(lines);

    if (!reportProgress(progress, 0.5f, "Generating atoms...")) {
        return ReadResult::Error("Cancelled");
    }

    // Create structure
    auto structure = std::make_unique<data::AtomicStructure>();
    structure->unitCell().setParameters(cell.a, cell.b, cell.c,
                                         cell.alpha, cell.beta, cell.gamma);

    // Apply symmetry operations to generate all atoms
    std::vector<std::tuple<float, float, float, int>> uniqueAtoms;

    for (const auto& site : sites) {
        int atomicNumber = data::ElementData::atomicNumberFromSymbol(site.symbol);
        if (atomicNumber == 0) {
            atomicNumber = data::ElementData::atomicNumberFromSymbol(
                extractElement(site.label));
        }

        for (const auto& op : symOps) {
            // Apply symmetry operation
            float fx = op.mat[0] * site.x + op.mat[1] * site.y +
                       op.mat[2] * site.z + op.mat[3];
            float fy = op.mat[4] * site.x + op.mat[5] * site.y +
                       op.mat[6] * site.z + op.mat[7];
            float fz = op.mat[8] * site.x + op.mat[9] * site.y +
                       op.mat[10] * site.z + op.mat[11];

            // Wrap to [0, 1)
            fx = fx - std::floor(fx);
            fy = fy - std::floor(fy);
            fz = fz - std::floor(fz);

            // Check for duplicates
            bool isDuplicate = false;
            for (const auto& [ux, uy, uz, ut] : uniqueAtoms) {
                float dx = std::abs(fx - ux);
                float dy = std::abs(fy - uy);
                float dz = std::abs(fz - uz);
                // Handle periodic boundaries
                if (dx > 0.5f) dx = 1.0f - dx;
                if (dy > 0.5f) dy = 1.0f - dy;
                if (dz > 0.5f) dz = 1.0f - dz;

                if (dx < 0.01f && dy < 0.01f && dz < 0.01f && ut == atomicNumber) {
                    isDuplicate = true;
                    break;
                }
            }

            if (!isDuplicate) {
                uniqueAtoms.emplace_back(fx, fy, fz, atomicNumber);
            }
        }
    }

    if (!reportProgress(progress, 0.8f, "Converting coordinates...")) {
        return ReadResult::Error("Cancelled");
    }

    // Convert fractional to Cartesian and add atoms
    structure->reserve(uniqueAtoms.size());
    for (const auto& [fx, fy, fz, type] : uniqueAtoms) {
        auto cart = structure->unitCell().fractionalToCartesian(fx, fy, fz);
        structure->addAtom(cart[0], cart[1], cart[2], type);
    }

    // Apply CPK colors and radii
    structure->updateColorsFromTypes();
    structure->updateRadiiFromTypes(0.5f);

    // Set name from data block if available
    std::string name;
    for (const auto& l : lines) {
        if (l.find("data_") == 0) {
            name = l.substr(5);
            break;
        }
    }
    if (name.empty()) {
        name = "CIF Structure";
    }
    structure->setName(name + " (" + std::to_string(structure->atomCount()) + " atoms)");

    reportProgress(progress, 1.0f, "Complete");

    return ReadResult::Success(std::move(structure));
}

} // namespace atom::io

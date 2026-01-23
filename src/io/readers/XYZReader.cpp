#include "XYZReader.h"
#include "../../data/AtomicStructure.h"
#include "../../data/ElementData.h"

#include <charconv>
#include <sstream>

namespace atom::io {

ReadResult XYZReader::read(std::string_view path, ProgressCallback progress) const {
    std::string contents = readFileContents(path);
    if (contents.empty()) {
        return ReadResult::Error("Could not read file: " + std::string(path));
    }

    auto result = parseXYZ(contents, path, progress);
    if (result.success && result.structure) {
        result.structure->setSourceFile(std::string(path));
    }
    return result;
}

ReadResult XYZReader::readFromString(std::string_view data,
                                      std::string_view sourceName,
                                      ProgressCallback progress) const {
    return parseXYZ(data, sourceName, progress);
}

ReadResult XYZReader::parseXYZ(std::string_view data, std::string_view sourceName,
                                ProgressCallback progress) const {
    std::istringstream stream{std::string{data}};
    std::string line;

    // Read atom count
    if (!std::getline(stream, line)) {
        return ReadResult::Error("Empty file");
    }

    // Trim whitespace
    while (!line.empty() && std::isspace(line.front())) line.erase(0, 1);
    while (!line.empty() && std::isspace(line.back())) line.pop_back();

    int atomCount = 0;
    auto [ptr, ec] = std::from_chars(line.data(), line.data() + line.size(), atomCount);
    if (ec != std::errc{} || atomCount <= 0) {
        return ReadResult::Error("Invalid atom count on first line");
    }

    // Read comment line
    std::string comment;
    if (!std::getline(stream, comment)) {
        return ReadResult::Error("Missing comment line");
    }

    // Create structure
    auto structure = std::make_unique<data::AtomicStructure>();
    structure->reserve(atomCount);

    // Extract name from comment if present
    while (!comment.empty() && std::isspace(comment.front())) comment.erase(0, 1);
    if (!comment.empty()) {
        structure->setName(comment);
    }

    if (!reportProgress(progress, 0.0f, "Reading atoms...")) {
        return ReadResult::Error("Cancelled");
    }

    // Read atoms
    int readCount = 0;
    while (std::getline(stream, line) && readCount < atomCount) {
        // Skip empty lines
        if (line.empty() || std::all_of(line.begin(), line.end(), ::isspace)) {
            continue;
        }

        // Parse: element x y z [optional columns]
        std::istringstream lineStream(line);
        std::string element;
        float x, y, z;

        if (!(lineStream >> element >> x >> y >> z)) {
            return ReadResult::Error("Invalid atom line at position " +
                                     std::to_string(readCount + 1));
        }

        // Look up element
        int atomicNumber = data::ElementData::atomicNumberFromSymbol(element);
        if (atomicNumber == 0) {
            // Try parsing as atomic number
            int num = 0;
            auto [p, e] = std::from_chars(element.data(),
                                          element.data() + element.size(), num);
            if (e == std::errc{} && num > 0 && num < data::ElementData::MAX_ELEMENTS) {
                atomicNumber = num;
            }
        }

        structure->addAtom(x, y, z, atomicNumber);
        ++readCount;

        // Report progress every 1000 atoms
        if (readCount % 1000 == 0) {
            float prog = static_cast<float>(readCount) / atomCount;
            if (!reportProgress(progress, prog,
                               "Reading atoms... " + std::to_string(readCount) +
                               "/" + std::to_string(atomCount))) {
                return ReadResult::Error("Cancelled");
            }
        }
    }

    if (readCount != atomCount) {
        return ReadResult::Error("Expected " + std::to_string(atomCount) +
                                " atoms but found " + std::to_string(readCount));
    }

    // Apply CPK colors and radii
    structure->updateColorsFromTypes();
    structure->updateRadiiFromTypes(0.5f); // 50% of covalent radii for ball-and-stick

    reportProgress(progress, 1.0f, "Complete");

    return ReadResult::Success(std::move(structure));
}

} // namespace atom::io

#pragma once

#include "../FileReader.h"

namespace atom::io {

/**
 * @brief Reader for XYZ format files
 *
 * Supports standard XYZ format:
 *   <atom_count>
 *   <comment line>
 *   <element> <x> <y> <z>
 *   ...
 *
 * Also supports extended XYZ with additional columns.
 */
class XYZReader : public FileReader {
public:
    std::string_view formatName() const override { return "XYZ"; }

    std::vector<std::string> supportedExtensions() const override {
        return {"xyz"};
    }

    ReadResult read(std::string_view path,
                    ProgressCallback progress = nullptr) const override;

    ReadResult readFromString(std::string_view data,
                              std::string_view sourceName = "buffer",
                              ProgressCallback progress = nullptr) const override;

private:
    ReadResult parseXYZ(std::string_view data, std::string_view sourceName,
                        ProgressCallback progress) const;
};

} // namespace atom::io

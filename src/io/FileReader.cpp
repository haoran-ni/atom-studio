#include "FileReader.h"
#include "../data/AtomicStructure.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace atom::io {

bool FileReader::canRead(std::string_view path) const {
    std::string ext = getExtension(path);
    auto exts = supportedExtensions();
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

ReadResult FileReader::readFromString(std::string_view /*data*/,
                                      std::string_view /*sourceName*/,
                                      ProgressCallback /*progress*/) const {
    return ReadResult::Error("Reading from string not implemented for this format");
}

bool FileReader::reportProgress(const ProgressCallback& callback,
                                float progress, std::string_view message) {
    if (callback) {
        return callback(progress, message);
    }
    return true; // Continue if no callback
}

std::string FileReader::getExtension(std::string_view path) {
    auto pos = path.rfind('.');
    if (pos == std::string_view::npos || pos == path.length() - 1) {
        return "";
    }
    std::string ext(path.substr(pos + 1));
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext;
}

std::string FileReader::readFileContents(std::string_view path) {
    std::ifstream file(std::string(path), std::ios::binary);
    if (!file) {
        return "";
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace atom::io

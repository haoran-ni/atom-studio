#include "FileReaderRegistry.h"
#include "readers/XYZReader.h"
#include "readers/LAMMPSDumpReader.h"
#include "readers/CIFReader.h"

#include <algorithm>

namespace atom::io {

FileReaderRegistry& FileReaderRegistry::instance() {
    static FileReaderRegistry instance;
    return instance;
}

FileReaderRegistry::FileReaderRegistry() {
    registerBuiltinReaders();
}

void FileReaderRegistry::registerBuiltinReaders() {
    registerReader(std::make_unique<XYZReader>());
    registerReader(std::make_unique<LAMMPSDumpReader>());
    registerReader(std::make_unique<CIFReader>());
}

void FileReaderRegistry::registerReader(std::unique_ptr<FileReader> reader) {
    // Map extensions to reader
    for (const auto& ext : reader->supportedExtensions()) {
        m_extensionMap[ext] = reader.get();
    }
    m_readers.push_back(std::move(reader));
}

const FileReader* FileReaderRegistry::readerForExtension(std::string_view extension) const {
    std::string ext(extension);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    auto it = m_extensionMap.find(ext);
    if (it != m_extensionMap.end()) {
        return it->second;
    }
    return nullptr;
}

const FileReader* FileReaderRegistry::readerForPath(std::string_view path) const {
    auto pos = path.rfind('.');
    if (pos == std::string_view::npos || pos == path.length() - 1) {
        return nullptr;
    }
    return readerForExtension(path.substr(pos + 1));
}

std::vector<std::string> FileReaderRegistry::supportedExtensions() const {
    std::vector<std::string> exts;
    for (const auto& [ext, reader] : m_extensionMap) {
        exts.push_back(ext);
    }
    std::sort(exts.begin(), exts.end());
    return exts;
}

std::string FileReaderRegistry::fileDialogFilter() const {
    std::string filter;

    // All supported files
    std::string allExts;
    for (const auto& [ext, reader] : m_extensionMap) {
        if (!allExts.empty()) allExts += " ";
        allExts += "*." + ext;
    }
    filter = "All Supported Files (" + allExts + ")";

    // Individual format filters
    for (const auto& reader : m_readers) {
        filter += ";;";
        filter += std::string(reader->formatName()) + " (";
        bool first = true;
        for (const auto& ext : reader->supportedExtensions()) {
            if (!first) filter += " ";
            filter += "*." + ext;
            first = false;
        }
        filter += ")";
    }

    // All files
    filter += ";;All Files (*)";

    return filter;
}

ReadResult FileReaderRegistry::readFile(std::string_view path,
                                         ProgressCallback progress) const {
    const FileReader* reader = readerForPath(path);
    if (!reader) {
        return ReadResult::Error("No reader available for file: " + std::string(path));
    }
    return reader->read(path, progress);
}

} // namespace atom::io

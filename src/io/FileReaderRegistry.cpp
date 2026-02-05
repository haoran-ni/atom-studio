#include "FileReaderRegistry.h"
#include "../python/ASEReader.h"
#include "../python/PythonRuntime.h"

#include <algorithm>
#include <iostream>

namespace atom::io {

/**
 * @brief Adapter to wrap ASEReader as a FileReader
 *
 * This allows the ASEReader (which requires Python GIL) to be used
 * through the standard FileReader interface.
 */
class ASEFileReader : public FileReader {
public:
    std::string_view formatName() const override {
        return "ASE";
    }

    std::vector<std::string> supportedExtensions() const override {
        return m_aseReader.supportedExtensions();
    }

    bool canRead(std::string_view path) const override {
        return m_aseReader.canRead(path);
    }

    ReadResult read(std::string_view path, ProgressCallback progress = nullptr) const override {
        // Acquire Python GIL before calling ASE
        python::PythonRuntime::GILGuard gil;

        auto result = m_aseReader.read(path, progress);

        ReadResult ioResult;
        ioResult.success = result.success;
        ioResult.errorMessage = result.errorMessage;
        ioResult.structure = std::move(result.structure);
        return ioResult;
    }

    ReadResult readFromString(std::string_view data,
                              std::string_view sourceName = "buffer",
                              ProgressCallback progress = nullptr) const override {
        // Acquire Python GIL before calling ASE
        python::PythonRuntime::GILGuard gil;

        // Determine format from source name extension
        std::string format = "xyz";  // Default
        auto dotPos = sourceName.rfind('.');
        if (dotPos != std::string_view::npos) {
            format = std::string(sourceName.substr(dotPos + 1));
            std::transform(format.begin(), format.end(), format.begin(), ::tolower);
        }

        auto result = m_aseReader.readFromString(data, format, sourceName, progress);

        ReadResult ioResult;
        ioResult.success = result.success;
        ioResult.errorMessage = result.errorMessage;
        ioResult.structure = std::move(result.structure);
        return ioResult;
    }

    std::string fileDialogFilter() const {
        return m_aseReader.fileDialogFilter();
    }

private:
    mutable python::ASEReader m_aseReader;
};

FileReaderRegistry& FileReaderRegistry::instance() {
    static FileReaderRegistry instance;
    return instance;
}

FileReaderRegistry::FileReaderRegistry() {
    registerBuiltinReaders();
}

void FileReaderRegistry::registerBuiltinReaders() {
    // Check if Python/ASE is available
    if (!python::PythonRuntime::instance().isInitialized()) {
        std::cerr << "[FileReaderRegistry] WARNING: Python not initialized. "
                  << "File reading will not be available." << std::endl;
        return;
    }

    // Register ASE reader (handles all supported formats)
    registerReader(std::make_unique<ASEFileReader>());
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

    // First try exact match
    auto it = m_extensionMap.find(ext);
    if (it != m_extensionMap.end()) {
        return it->second;
    }

    // Check for wildcard reader (accepts all files)
    it = m_extensionMap.find("*");
    if (it != m_extensionMap.end()) {
        return it->second;
    }

    return nullptr;
}

const FileReader* FileReaderRegistry::readerForPath(std::string_view path) const {
    // First, check if any reader can handle this path directly
    for (const auto& reader : m_readers) {
        if (reader->canRead(path)) {
            return reader.get();
        }
    }

    // Fallback to extension-based lookup
    auto pos = path.rfind('.');
    if (pos != std::string_view::npos && pos != path.length() - 1) {
        return readerForExtension(path.substr(pos + 1));
    }

    // No extension - check for wildcard reader
    return readerForExtension("*");
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
    // Use ASE's file dialog filter directly if available
    if (!m_readers.empty()) {
        auto* aseReader = dynamic_cast<ASEFileReader*>(m_readers[0].get());
        if (aseReader) {
            return aseReader->fileDialogFilter();
        }
    }

    // Fallback: build from extension map
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

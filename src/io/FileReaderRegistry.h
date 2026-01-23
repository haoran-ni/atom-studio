#pragma once

#include "FileReader.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace atom::io {

/**
 * @brief Registry for file readers
 *
 * Manages file reader instances and provides lookup by extension.
 * Thread-safe singleton pattern.
 */
class FileReaderRegistry {
public:
    /**
     * @brief Get the singleton instance
     */
    static FileReaderRegistry& instance();

    // Non-copyable
    FileReaderRegistry(const FileReaderRegistry&) = delete;
    FileReaderRegistry& operator=(const FileReaderRegistry&) = delete;

    /**
     * @brief Register a file reader
     * @param reader Reader instance (ownership transferred)
     */
    void registerReader(std::unique_ptr<FileReader> reader);

    /**
     * @brief Get reader for a file extension
     * @param extension File extension (lowercase, no dot)
     * @return Reader or nullptr if not found
     */
    const FileReader* readerForExtension(std::string_view extension) const;

    /**
     * @brief Get reader for a file path
     * @param path File path
     * @return Reader or nullptr if not found
     */
    const FileReader* readerForPath(std::string_view path) const;

    /**
     * @brief Get all registered readers
     */
    const std::vector<std::unique_ptr<FileReader>>& readers() const { return m_readers; }

    /**
     * @brief Get list of all supported extensions
     */
    std::vector<std::string> supportedExtensions() const;

    /**
     * @brief Generate file filter string for file dialogs
     * @return Filter string like "XYZ Files (*.xyz);;LAMMPS Dump (*.dump)"
     */
    std::string fileDialogFilter() const;

    /**
     * @brief Read a file using appropriate reader
     * @param path File path
     * @param progress Optional progress callback
     * @return Read result
     */
    ReadResult readFile(std::string_view path,
                        ProgressCallback progress = nullptr) const;

private:
    FileReaderRegistry();
    void registerBuiltinReaders();

    std::vector<std::unique_ptr<FileReader>> m_readers;
    std::unordered_map<std::string, FileReader*> m_extensionMap;
};

} // namespace atom::io

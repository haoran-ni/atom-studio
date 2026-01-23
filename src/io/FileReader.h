#pragma once

#include "../data/AtomicStructure.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace atom::io {

/**
 * @brief Result of a file read operation
 */
struct ReadResult {
    bool success = false;
    std::string errorMessage;
    std::unique_ptr<data::AtomicStructure> structure;

    static ReadResult Success(std::unique_ptr<data::AtomicStructure> s) {
        ReadResult r;
        r.success = true;
        r.structure = std::move(s);
        return r;
    }

    static ReadResult Error(std::string_view message) {
        ReadResult r;
        r.success = false;
        r.errorMessage = std::string(message);
        return r;
    }
};

/**
 * @brief Progress callback type
 * @param progress Value from 0.0 to 1.0
 * @param message Status message
 * @return false to cancel reading
 */
using ProgressCallback = std::function<bool(float progress, std::string_view message)>;

/**
 * @brief Abstract interface for file readers
 *
 * All file format readers implement this interface.
 * Readers are stateless and can be reused.
 */
class FileReader {
public:
    virtual ~FileReader() = default;

    /**
     * @brief Get the format name
     */
    virtual std::string_view formatName() const = 0;

    /**
     * @brief Get supported file extensions (lowercase, without dot)
     */
    virtual std::vector<std::string> supportedExtensions() const = 0;

    /**
     * @brief Check if this reader can handle the given file
     * @param path File path
     * @return true if this reader can read the file
     */
    virtual bool canRead(std::string_view path) const;

    /**
     * @brief Read an atomic structure from file
     * @param path Path to the file
     * @param progress Optional progress callback
     * @return Read result with structure or error
     */
    virtual ReadResult read(std::string_view path,
                            ProgressCallback progress = nullptr) const = 0;

    /**
     * @brief Read from a string buffer
     * @param data File contents
     * @param sourceName Name for error messages
     * @param progress Optional progress callback
     * @return Read result
     */
    virtual ReadResult readFromString(std::string_view data,
                                      std::string_view sourceName = "buffer",
                                      ProgressCallback progress = nullptr) const;

protected:
    /**
     * @brief Report progress, checking for cancellation
     * @return false if cancelled
     */
    static bool reportProgress(const ProgressCallback& callback,
                               float progress, std::string_view message);

    /**
     * @brief Get file extension from path (lowercase)
     */
    static std::string getExtension(std::string_view path);

    /**
     * @brief Read entire file into string
     */
    static std::string readFileContents(std::string_view path);
};

} // namespace atom::io

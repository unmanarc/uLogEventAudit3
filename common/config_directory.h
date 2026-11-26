/** @file @brief Ordered JSON object collections stored as individually reusable files. */
#pragma once
#include "audit_json.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace UL3::JSON {
/** @brief Read immediate regular .json files in filename order; missing directories are empty.
 * @param path Collection directory. Each document must contain one object.
 * @return All definitions, preserving filename order and nested arrays.
 * @throws std::exception On malformed documents, nonregular JSON entries or I/O errors.
 */
inline boost::json::array readConfigDirectory(const std::string &path)
{
    namespace fs = std::filesystem;
    boost::json::array result;
    if (!fs::exists(path))
    {
        return result;
    }
    std::vector<fs::path> files;
    for (const auto &entry : fs::directory_iterator(path))
    {
        if (entry.path().extension() == ".json")
        {
            if (!fs::is_regular_file(entry.symlink_status()))
            {
                throw std::runtime_error("Expected a regular JSON file: " + entry.path().string());
            }
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const auto &file : files)
    {
        std::ifstream input(file, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Cannot read configuration: " + file.string());
        }
        const std::string document{std::istreambuf_iterator<char>(input), {}};
        const auto value = boost::json::parse(document);
        if (input.bad() || !value.is_object())
        {
            throw std::runtime_error("Expected a JSON object: " + file.string());
        }
        result.push_back(value);
    }
    return result;
}

/** @brief Build an ordered filename with an escaped identity, preventing path traversal.
 * @param index Zero-based position in the collection.
 * @param definition Object whose id or name labels the file.
 */
inline std::string configFilename(size_t index, const boost::json::object &definition)
{
    std::ostringstream name;
    name << std::setw(8) << std::setfill('0') << index << '-';
    const auto *identity = definition.if_contains("id");
    if (!identity)
    {
        identity = definition.if_contains("name");
    }
    if (identity && identity->is_string())
    {
        // Explicit ASCII allowlist keeps filenames independent of the process locale.
        const std::string allowed = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
        for (const unsigned char byte : identity->as_string())
        {
            if (allowed.find(static_cast<char>(byte)) != std::string::npos)
            {
                name << static_cast<char>(byte);
            }
            else
            {
                name << '%' << std::hex << std::setw(2) << static_cast<unsigned>(byte) << std::dec;
            }
        }
    }
    else
    {
        name << "entry";
    }
    // Bound component length even for long user-supplied names; the index is unique.
    return name.str().substr(0, 240) + ".json";
}

/** @brief Atomically publish a complete collection using a sibling staging directory.
 * @param path Target directory; its parent must exist and allow directory replacement.
 * @param definitions One object per file, with numeric prefixes preserving array order.
 * @throws std::exception Before publication on validation or persistence failure.
 * @note Linux renameat2 exchanges existing directories without exposing partial collections.
 */
inline void writeConfigDirectory(const std::string &path, const boost::json::array &definitions)
{
    namespace fs = std::filesystem;
    fs::path target = fs::path(path).lexically_normal();
    // Accept conventional directory paths with a trailing slash.
    if (target.filename().empty())
    {
        target = target.parent_path();
    }
    if (target.filename().empty() || target.filename() == "." || target.filename() == "..")
    {
        throw std::invalid_argument("Expected a collection directory name");
    }
    if (fs::exists(target) && !fs::is_directory(fs::symlink_status(target)))
    {
        throw std::runtime_error("Expected a configuration directory: " + path);
    }
    std::string temporary = target.string() + ".XXXXXX";
    if (!mkdtemp(temporary.data()))
    {
        throw std::system_error(errno, std::generic_category(), "Cannot stage configuration directory");
    }
    try
    {
        // Preserve unrelated notes and disabled definitions during GUI saves.
        if (fs::exists(target))
        {
            for (const auto &entry : fs::directory_iterator(target))
            {
                if (entry.path().extension() != ".json")
                {
                    fs::copy(entry.path(), fs::path(temporary) / entry.path().filename(),
                             fs::copy_options::recursive | fs::copy_options::copy_symlinks);
                }
            }
        }
        for (size_t index = 0; index < definitions.size(); ++index)
        {
            const auto &definition = definitions[index];
            if (!definition.is_object())
            {
                throw std::invalid_argument("Each configuration entry must be an object");
            }
            const auto file = fs::path(temporary) / configFilename(index, definition.as_object());
            const auto document = toStyledString(definition);
            const int descriptor = open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (descriptor < 0)
            {
                throw std::system_error(errno, std::generic_category(), "Cannot create configuration entry");
            }
            int failure = 0;
            size_t offset = 0;
            while (offset < document.size())
            {
                const auto count = write(descriptor, document.data() + offset, document.size() - offset);
                if (count < 0 && errno == EINTR)
                {
                    continue;
                }
                if (count <= 0)
                {
                    failure = count < 0 ? errno : EIO;
                    break;
                }
                offset += static_cast<size_t>(count);
            }
            if (!failure && fsync(descriptor) != 0)
            {
                failure = errno;
            }
            if (close(descriptor) != 0 && !failure)
            {
                failure = errno;
            }
            if (failure)
            {
                throw std::system_error(failure, std::generic_category(), "Cannot save configuration entry");
            }
        }
        if (fs::exists(target))
        {
            if (syscall(SYS_renameat2, AT_FDCWD, temporary.c_str(), AT_FDCWD, target.c_str(), RENAME_EXCHANGE) != 0)
            {
                throw std::system_error(errno, std::generic_category(), "Cannot publish configuration directory");
            }
        }
        else
        {
            fs::rename(temporary, target);
        }
    }
    catch (...)
    {
        std::error_code ignored;
        fs::remove_all(temporary, ignored);
        throw;
    }
    // Publication succeeded: cleanup must never report the committed update as failed.
    std::error_code ignored;
    fs::remove_all(temporary, ignored);
}
} // namespace UL3::JSON

/** @file @brief Validate endpoint files and save GUI changes without replacing live workers. */
#include "endpointconfig.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace UL3::LOG2JSON {
namespace {
/** @brief Require a nonempty string so unusable endpoint definitions fail before saving. */
std::string text(const boost::json::object &object, const char *key)
{
    const auto *value = object.if_contains(key);
    if (!value || !value->is_string() || value->as_string().empty() ||
        value->as_string().find('\0') != boost::json::string::npos)
    {
        throw std::invalid_argument(std::string("Expected a nonempty string: ") + key);
    }
    return std::string(value->as_string());
}

/** @brief Require an integer within the supported range and supply an optional default. */
void number(boost::json::object &object, const char *key, uint64_t minimum, uint64_t maximum, uint64_t fallback)
{
    auto *value = object.if_contains(key);
    if (!value)
    {
        object[key] = fallback;
        value = object.if_contains(key);
    }
    if ((!value->is_int64() && !value->is_uint64()) ||
        (value->is_int64() && value->as_int64() < 0))
    {
        throw std::invalid_argument(std::string("Expected an unsigned integer: ") + key);
    }
    const auto integer = value->is_uint64() ? value->as_uint64() : static_cast<uint64_t>(value->as_int64());
    if (integer < minimum || integer > maximum)
    {
        throw std::invalid_argument(std::string("Value out of range: ") + key);
    }
    // Normalize signed/unsigned representations for reliable snapshot comparisons.
    *value = integer;
}

/** @brief Atomically replace the JSON file using a private temporary file in its directory. */
void persist(const std::string &path, const std::string &document)
{
    std::string temporary = path + ".XXXXXX";
    const int descriptor = mkstemp(temporary.data());
    if (descriptor < 0)
    {
        throw std::system_error(errno, std::generic_category(), "Cannot create endpoint configuration");
    }
    int failure = 0;
    size_t offset = 0;
    while (offset < document.size())
    {
        const auto written = write(descriptor, document.data() + offset, document.size() - offset);
        if (written < 0 && errno == EINTR)
        {
            continue;
        }
        if (written <= 0)
        {
            failure = written < 0 ? errno : EIO;
            break;
        }
        offset += static_cast<size_t>(written);
    }
    if (!failure && fsync(descriptor) != 0)
    {
        failure = errno;
    }
    if (close(descriptor) != 0 && !failure)
    {
        failure = errno;
    }
    if (!failure && std::rename(temporary.c_str(), path.c_str()) != 0)
    {
        failure = errno;
    }
    if (failure)
    {
        std::remove(temporary.c_str());
        throw std::system_error(failure, std::generic_category(), "Cannot save endpoint configuration");
    }
}
} // namespace

boost::json::array EndpointConfig::validate(const boost::json::array &definitions) const
{
    auto result = definitions;
    std::set<std::string> identities;
    std::set<std::pair<std::string, uint64_t>> listeners;
    for (auto &definition : result)
    {
        if (!definition.is_object())
        {
            throw std::invalid_argument("Each endpoint must be an object");
        }
        auto &object = definition.as_object();
        const auto id = text(object, "id");
        if (!identities.insert(id).second)
        {
            throw std::invalid_argument("Duplicate endpoint id: " + id);
        }
        text(object, "Description");
        const auto address = text(object, inputs ? "ListenAddr" : "Host");
        const auto portKey = inputs ? "ListenPort" : "Port";
        // Port zero is not a persistent, addressable service endpoint.
        number(object, portKey, 1, 65535, 0);
        if (inputs)
        {
            if (!listeners.emplace(address, object.at(portKey).as_uint64()).second)
            {
                throw std::invalid_argument("Duplicate TCP listener");
            }
        }
        else
        {
            number(object, "TransmissionMode", 0, 1, 0);
            number(object, "TimeoutSecs", 1, std::numeric_limits<uint32_t>::max(), 10);
            number(object, "ReconnectSleepTimeInSecs", 1, std::numeric_limits<uint32_t>::max(), 3);
            number(object, "MaxQueuedItems", 1, std::numeric_limits<size_t>::max(), 500000);
            number(object, "QueuePushTimeoutInMS", 0, std::numeric_limits<uint32_t>::max(), 0);
        }
    }
    return result;
}

boost::json::array EndpointConfig::load(const std::string &file)
{
    std::scoped_lock lock(mutex);
    std::ifstream input(file, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Cannot read endpoint configuration: " + file);
    }
    const std::string document{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad())
    {
        throw std::runtime_error("Cannot read endpoint configuration: " + file);
    }
    const auto value = boost::json::parse(document);
    if (!value.is_array())
    {
        throw std::invalid_argument("Endpoint configuration must be an array: " + file);
    }
    const auto definitions = validate(value.as_array());
    path = file;
    saved = definitions;
    running = definitions;
    return definitions;
}

boost::json::object EndpointConfig::read()
{
    std::scoped_lock lock(mutex);
    return {{"ok", true}, {"modified", saved != running}, {inputs ? "inputs" : "outputs", saved}};
}

void EndpointConfig::replace(const boost::json::array &definitions)
{
    auto validated = validate(definitions);
    std::scoped_lock lock(mutex);
    if (path.empty())
    {
        throw std::runtime_error("Endpoint configuration path is not initialized");
    }
    if (saved != validated)
    {
        persist(path, UL3::JSON::toStyledString(validated));
        saved = std::move(validated);
    }
}
} // namespace UL3::LOG2JSON

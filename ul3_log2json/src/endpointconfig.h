/** @file @brief Validated, persistent TCP endpoint definitions awaiting a service restart. */
#pragma once

#include <audit_json.h>
#include <mutex>
#include <string>

namespace UL3::LOG2JSON {

/** @brief Keep saved endpoint settings separate from the running service snapshot. */
class EndpointConfig
{
public:
    /** @brief Select listener or destination validation rules. */
    explicit EndpointConfig(bool inputs) : inputs(inputs) {}
    /** @brief Read and validate an ordered directory of JSON objects before initializing the running snapshot. */
    boost::json::array load(const std::string &path);
    /** @brief Return saved definitions and whether they differ from running settings. */
    boost::json::object read();
    /** @brief Validate and atomically save a replacement without touching running workers. */
    void replace(const boost::json::array &definitions);

private:
    /** @brief Validate endpoint types, bounds and identities, filling optional defaults. */
    boost::json::array validate(const boost::json::array &definitions) const;

    bool inputs;
    std::mutex mutex;
    std::string path;
    boost::json::array saved, running;
};
} // namespace UL3::LOG2JSON

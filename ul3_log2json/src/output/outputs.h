/**
 * @file
 * @brief Declarations and contracts for ul3_log2json/src/output/outputs.
 */
#pragma once

#include "../endpointconfig.h"
#include <list>
#include <memory>
#include <mutex>
#include <string>

#include "../output/output_base.h"

namespace UL3::LOG2JSON::Output {

/** @brief Own the configured destinations receiving each reconstructed audit event. */
class Outputs
{
public:
    /**
     * @brief Manage configured event destinations.
     */
    Outputs();

    /**
     * @brief Return a JSON snapshot of counters and current resource usage.
     */
    static boost::json::array getStats();
    /**
     * @brief Load and register endpoints from a validated JSON array file.
     * @param file Path of the JSON configuration file.
     */
    static void loadConfig(const std::string &file);
    /**
     * @brief Start the workers for all configured inputs or outputs.
     */
    static void startThreads();
    /** @brief Return saved endpoint definitions and pending restart status. */
    static boost::json::object readConfig();
    /** @brief Validate and save settings for the next service restart. */
    static void writeConfig(const boost::json::array &definitions);
    /**
     * @brief Send an event to each configured output under the output registry lock.
     * @param eventJson Serialized event copied to the output queues.
     * @param eventId Event identity as seconds, milliseconds and a 64-bit serial number.
     */
    static void dispatchEvent(const boost::json::object &eventJson, const std::tuple<time_t, uint32_t, uint64_t> &eventId);

private:
    static EndpointConfig configuration;
    static std::list<std::unique_ptr<OutputBase>> outputs;
    static std::mutex registryMutex;
};

} // namespace UL3::LOG2JSON::Output

/**
 * @file
 * @brief Declarations and contracts for uanlz_log2json/src/output/outputs.
 */
#pragma once

#include <string>
#include <list>
#include <mutex>
#include <memory>

#include "../output/output_base.h"

namespace UANLZ::LOG2JSON::Output
{

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
    static json getStats();
    /**
     * @brief Load and register the configured TCP input or output definitions.
     * @param file Path of the INI configuration file.
     */
    static void loadConfig(const std::string &file);
    /**
     * @brief Start the workers for all configured inputs or outputs.
     */
    static void startThreads();
    /**
     * @brief Send an event to each configured output under the output registry lock.
     * @param eventJson Serialized event copied to the output queues.
     * @param eventId Event identity as seconds, milliseconds and a 64-bit serial number.
     */
    static void dispatchEvent(const json &eventJson, const std::tuple<time_t, uint32_t, uint64_t> &eventId);

private:
    static std::list<std::unique_ptr<OutputBase>> outputs;
    static std::mutex registryMutex;
};

} // namespace UANLZ::LOG2JSON::Output

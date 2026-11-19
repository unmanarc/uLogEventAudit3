/**
 * @file
 * @brief Declarations and contracts for uanlz_log2json/src/output/output_base.
 */
#pragma once

#include "../events/audit_event.h"
#include <audit_json.h>

namespace UANLZ::LOG2JSON::Output {

/** @brief Define the contract for asynchronous audit event destinations. */
class OutputBase
{
public:
    /**
     * @brief Create the common state for an event destination.
     */
    OutputBase();
    /**
     * @brief Release the resources owned by this OutputBase instance.
     */
    virtual ~OutputBase();

    /**
     * @brief Load the configuration required to initialize this input or output.
     * @param file Path of the INI configuration file.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    virtual bool loadConfig(const std::string &file) = 0;
    /**
     * @brief Return a JSON snapshot of counters and current resource usage.
     */
    virtual json getStats() = 0;
    /**
     * @brief Copy an event into the Mantids30 output queue or count a rejected enqueue.
     * @param eventJson Serialized event copied to the output queues.
     * @param eventId Event identity as seconds, milliseconds and a 64-bit serial number.
     */
    virtual void logAuditEvent(const json &eventJson, const std::tuple<time_t, uint32_t, uint64_t> &eventId) = 0;
    /**
     * @brief Launch the service-lifetime TCP output worker.
     * @note Workers use the existing service-lifetime shutdown model.
     */
    virtual void startThread() = 0;
    /**
     * @brief Consume queued events and retry TCP delivery for the lifetime of the service.
     */
    virtual void process() = 0;
    /**
     * @brief Return the configured human-readable destination description.
     */
    std::string getDescription() const;

protected:
    std::string description;
};

} // namespace UANLZ::LOG2JSON::Output

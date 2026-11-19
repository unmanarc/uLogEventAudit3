/**
 * @file
 * @brief Declarations and contracts for uanlz_log2json/src/events/events_manager.
 */
#pragma once

#include "audit_host.h"

#include <atomic>
#include <map>
#include <string>

namespace UANLZ::LOG2JSON::AuditdEvents {

/** @brief Route incoming records to shared host buffers without holding the registry lock during processing. */
class EventsManager
{
public:
    /**
     * @brief Provide access to the shared host event registry.
     */
    EventsManager();
    /**
     * @brief Destroy this facade without resetting the shared host registry.
     */
    ~EventsManager() = default;

    /**
     * @brief Return a JSON snapshot of counters and current resource usage.
     */
    static json getStats();

    /**
     * @brief Run the periodic host expiration loop for the lifetime of the service.
     * @note Workers use the existing service-lifetime shutdown model.
     */
    static void collectExpiredEvents();
    /**
     * @brief Launch the service-lifetime worker that flushes expired events.
     * @note Workers use the existing service-lifetime shutdown model.
     */
    static void startGarbageCollector();

    /**
     * @brief Buffer a record in its host event and enqueue completed events when expiration is not enforced.
     * @param hostId Hostname and source address used as the host registry key.
     * @param eventId Event identity as seconds, milliseconds and a 64-bit serial number.
     * @param eventType Auditd record type used to detect event completion.
     * @param recordData Owned record text to append or move into the event.
     */
    static void insertClassContents(const AuditHostId &hostId, const std::tuple<time_t, uint32_t, uint64_t> &eventId, const std::string &eventType, std::string recordData);

private:
    using HostMap = std::map<AuditHostId, std::shared_ptr<AuditHost>>;
    /**
     * @brief Copy shared host owners while holding only the host registry mutex.
     */
    static HostMap snapshotHosts();
    static HostMap hostsById;
    static std::mutex hostsMutex;
};

} // namespace UANLZ::LOG2JSON::AuditdEvents

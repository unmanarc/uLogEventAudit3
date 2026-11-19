/**
 * @file
 * @brief Declarations and contracts for uanlz_log2json/src/events/audit_host.
 */
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "audit_event.h"

namespace UANLZ::LOG2JSON::AuditdEvents {

/** @brief Synchronize incomplete events for one source and enqueue completed or expired events. */
class AuditHost
{
public:
    /**
     * @brief Create a host event buffer using the configured expiration policy.
     */
    AuditHost();
    /**
     * @brief Release the resources owned by this AuditHost instance.
     */
    ~AuditHost() = default;

    /**
     * @brief Buffer a record in its host event and enqueue completed events when expiration is not enforced.
     * @param eventId Event identity as seconds, milliseconds and a 64-bit serial number.
     * @param eventType Auditd record type used to detect event completion.
     * @param recordData Owned record text to append or move into the event.
     */
    void insertClassContents(const std::tuple<time_t, uint32_t, uint64_t> &eventId, const std::string &eventType, std::string recordData);

    /**
     * @brief Set the host identity when the buffer has no hostname yet.
     * @param value Hostname and source address to associate with the buffer or event.
     */
    void setHostId(const AuditHostId &value);

    /**
     * @brief Remove expired host events and transfer them to the distribution queue.
     * @param maxEventAgeSeconds Maximum age before an event is flushed, including incomplete events.
     */
    void flushExpiredEvents(const uint64_t &maxEventAgeSeconds);

    /**
     * @brief Return the number of host events still awaiting completion or expiration.
     */
    size_t getPendingEventsCount();
    /**
     * @brief Return the number of host events rejected by the distribution queue.
     */
    uint64_t getDroppedEventsCount() const;
    /**
     * @brief Return the number of host events accepted by the distribution queue.
     */
    uint64_t getProcessedEventsCount() const;

private:
    bool enforceMaxAge;
    std::mutex eventsMutex;
    std::map<std::tuple<time_t, uint32_t, uint64_t>, std::unique_ptr<AuditEvent>> auditEvents;
    AuditHostId hostId;
    std::atomic<uint64_t> droppedEventsCount, processedEventsCount;
};

} // namespace UANLZ::LOG2JSON::AuditdEvents

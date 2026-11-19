/**
 * @file
 * @brief Declarations and contracts for uanlz_log2json/src/events/audit_event.
 */
#pragma once

#include "audit_class.h"
#include <chrono>
#include <list>

namespace UANLZ::LOG2JSON::AuditdEvents {

/** @brief Composite event-source identity used as the host registry key. */
struct AuditHostId
{
    std::string hostname;
    std::string ipAddress;

    /**
     * @brief Compare the hostname and source address for host identity.
     * @param other Host identity to compare with this one.
     * @return True when both identity fields match.
     */
    bool operator==(const AuditHostId &other) const { return hostname == other.hostname && ipAddress == other.ipAddress; }

    /**
     * @brief Order hosts by hostname and then by source address for map keys.
     * @param other Host identity to compare with this one.
     * @return True when this host sorts before the other host.
     */
    bool operator<(const AuditHostId &other) const { return hostname < other.hostname || (hostname == other.hostname && ipAddress < other.ipAddress); }
};

/** @brief Own auditd records sharing one timestamp and serial number. */
class AuditEvent
{
public:
    /**
     * @brief Create an event and start its monotonic age timer.
     */
    AuditEvent();
    /**
     * @brief Release the resources owned by this AuditEvent instance.
     */
    ~AuditEvent() = default;

    /**
     * @brief Return the elapsed whole seconds since event creation using a monotonic clock.
     * @return Whole elapsed seconds unaffected by wall-clock adjustments.
     */
    uint64_t getAgeSeconds();

    /**
     * @brief Add auditd record text to its event, merging repeated types except PATH.
     * @param eventType Auditd record type controlling interpretation and event completion.
     * @param recordData Owned record text to append or move into the event.
     * @return For an event, true if the record type marks event completion.
     */
    bool insertClassContents(const std::string &eventType, std::string recordData);

    /**
     * @brief Return the audit timestamp, milliseconds and 64-bit serial number.
     */
    std::tuple<time_t, uint32_t, uint64_t> getEventId() const;
    /**
     * @brief Set the timestamp and serial number identifying this event.
     * @param value Timestamp and serial number to store.
     */
    void setEventId(const std::tuple<time_t, uint32_t, uint64_t> &value);

    /**
     * @brief Return the hostname and source address associated with this event.
     */
    AuditHostId getHostId() const;
    /**
     * @brief Associate the event or host buffer with its hostname and source address.
     * @param value Hostname and source address to associate with the buffer or event.
     */
    void setHostId(const AuditHostId &value);

    /**
     * @brief Parse the variables in every buffered auditd record.
     * @return True after all buffered records have been processed.
     */
    bool process();

    /**
     * @brief Return the distinct auditd record types held by this event.
     */
    std::set<std::string> getClassNames();
    /**
     * @brief Report whether more than one record exists for the requested type.
     * @param groupName Auditd record type to look up.
     * @return True if at least two records have the requested type.
     */
    bool hasMultipleClassRecords(const std::string &groupName);
    /**
     * @brief Return borrowed pointers to all records of the requested type.
     * @param groupName Auditd record type to look up.
     * @return Pointers valid while the owning event and its records remain alive.
     */
    std::list<AuditClass *> getClassRecords(const std::string &groupName);
    /**
     * @brief Return a borrowed pointer to the first matching record, or nullptr.
     * @param groupName Auditd record type to look up.
     * @return Borrowed record pointer, or nullptr when the type is absent.
     */
    AuditClass *getFirstClassRecord(const std::string &groupName);

    /**
     * @brief Serialize parsed auditd data using the existing event JSON schema.
     */
    boost::json::object getJson();

private:
    // eventType -> ClassEvent
    std::multimap<std::string, AuditClass> classRecords;
    std::tuple<time_t, uint32_t, uint64_t> eventId;
    AuditHostId hostId;
    std::chrono::steady_clock::time_point creationTime;
};

} // namespace UANLZ::LOG2JSON::AuditdEvents

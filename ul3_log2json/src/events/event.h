/** @file @brief Common ownership and serialization contract for queued events. */
#pragma once

#include <audit_json.h>
#include <cstdint>
#include <ctime>
#include <tuple>
#include <utility>

namespace UL3::LOG2JSON::Events {

/** @brief An event delivered through the shared output distribution queue. */
class Event
{
public:
    /** @brief Destroy derived events through shared queue ownership. */
    virtual ~Event() = default;
    /** @brief Prepare buffered records for serialization. */
    virtual bool process() { return true; }
    /** @brief Serialize this event for the configured JSON outputs. */
    virtual boost::json::object getJson() = 0;
    /** @brief Return the identity used in output diagnostics. */
    virtual std::tuple<time_t, uint32_t, uint64_t> getEventId() const = 0;
};

/** @brief A syslog message already rendered from a module's JSON template. */
class SyslogEvent final : public Event
{
public:
    /** @brief Own the rendered JSON and assign a reception timestamp and serial. */
    explicit SyslogEvent(boost::json::object document);
    /** @brief Return the module's JSON without adding or overwriting fields. */
    boost::json::object getJson() override { return document; }
    /** @brief Return reception time and the process-local serial number. */
    std::tuple<time_t, uint32_t, uint64_t> getEventId() const override { return eventId; }

private:
    boost::json::object document;
    std::tuple<time_t, uint32_t, uint64_t> eventId;
};

} // namespace UL3::LOG2JSON::Events

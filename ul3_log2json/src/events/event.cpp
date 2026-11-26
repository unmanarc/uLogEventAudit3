/** @file @brief Assign identities to rendered syslog events. */
#include "event.h"
#include <atomic>
#include <chrono>

namespace UL3::LOG2JSON::Events {
SyslogEvent::SyslogEvent(boost::json::object document) : document(std::move(document))
{
    static std::atomic<uint64_t> serial{0};
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    eventId = {static_cast<time_t>(milliseconds / 1000), static_cast<uint32_t>(milliseconds % 1000), ++serial};
}
} // namespace UL3::LOG2JSON::Events

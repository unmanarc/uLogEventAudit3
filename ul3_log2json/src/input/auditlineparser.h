/** @file @brief Decode raw auditd and forwarded syslog record headers. */
#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <tuple>

namespace UL3::LOG2JSON::Input {

/** @brief Syslog framing shared by auditd and configurable module parsers. */
struct SyslogLine
{
    std::string hostName;
    std::string sourceIp;
    std::string logger;
    std::string message;
};

/** @brief Extract RFC3164 or RFC5424 framing, excluding the PID from the logger. */
bool parseSyslogLine(const std::string &line, SyslogLine &result);

/** @brief Parsed routing fields and the untouched audit record payload. */
struct AuditLine
{
    std::string hostName;
    std::string sourceIp;
    std::string type;
    std::string payload;
    std::tuple<time_t, uint32_t, uint64_t> eventId;
};

enum class AuditLineStatus : std::uint8_t
{
    Parsed,
    Ignored,
    Invalid
};

/**
 * @brief Decode supported syslog framing and the auditd event identifier.
 * @param line Complete received line, without its line delimiter.
 * @param result Assigned only for a valid audit record.
 * @return Parsed for audit records, Ignored for other syslog programs or empty lines,
 *         Invalid for malformed framing, missing fields or overflowing numbers.
 */
AuditLineStatus parseAuditLine(const std::string &line, AuditLine &result);

} // namespace UL3::LOG2JSON::Input

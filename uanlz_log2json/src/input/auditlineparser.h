/** @file @brief Decode raw auditd and forwarded syslog record headers. */
#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <tuple>

namespace UANLZ::LOG2JSON::Input {

/** @brief Parsed routing fields and the untouched audit record payload. */
struct AuditLine
{
    std::string hostName;
    std::string sourceIp;
    std::string type;
    std::string payload;
    std::tuple<time_t, uint32_t, uint64_t> eventId{};
};

enum class AuditLineStatus { Parsed, Ignored, Invalid };

/**
 * @brief Decode supported syslog framing and the auditd event identifier.
 * @param line Complete received line, without its line delimiter.
 * @param result Assigned only for a valid audit record.
 * @return Parsed for audit records, Ignored for other syslog programs or empty lines,
 *         Invalid for malformed framing, missing fields or overflowing numbers.
 */
AuditLineStatus parseAuditLine(const std::string &line, AuditLine &result);

} // namespace UANLZ::LOG2JSON::Input

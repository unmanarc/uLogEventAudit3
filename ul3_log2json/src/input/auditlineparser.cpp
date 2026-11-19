/** @file @brief Regex-based extraction of syslog and auditd headers. */
#include "auditlineparser.h"

#include <boost/regex.hpp>
#include <charconv>
#include <utility>

namespace UL3::LOG2JSON::Input {
namespace {
/** @brief Convert a complete captured decimal field without accepting overflow. */
template<typename T>
bool readNumber(const boost::ssub_match &field, T &result)
{
    const auto text = field.str();
    const auto conversion = std::from_chars(text.data(), text.data() + text.size(), result);
    return conversion.ec == std::errc{} && conversion.ptr == text.data() + text.size();
}
} // namespace

AuditLineStatus parseAuditLine(const std::string &line, AuditLine &result)
{
    if (line.empty())
    {
        return AuditLineStatus::Ignored;
    }

    // Compile once; each invocation owns its captures and numeric conversions.
    static const boost::regex syslogPattern(
        R"((?:<[^>\r\n]*>(?:<([^<>\r\n]+)>)?)?(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) +[0-9]{1,2} +[0-9]{2}:[0-9]{2}:[0-9]{2} +([^ \t\r\n]+) +([^ :\[\t\r\n]+)(?:\[[0-9]+\])?:[ \t]*)");
    static const boost::regex auditPattern(R"([ \t]*(?:node=([^ \t\r\n]+)[ \t]+)?type=([^ \t\r\n]+)[ \t]+msg=audit\(([0-9]+)\.([0-9]+):([0-9]+)\):)");

    AuditLine parsed;
    auto start = line.cbegin();
    boost::smatch fields;
    if (boost::regex_search(start, line.cend(), fields, syslogPattern, boost::match_continuous))
    {
        const auto program = fields[3].str();
        if (program != "audispd" && program != "audisp-syslog")
        {
            return AuditLineStatus::Ignored;
        }
        parsed.sourceIp = fields[1].str();
        parsed.hostName = fields[2].str();
        start = fields[0].second;
    }

    if (!boost::regex_search(start, line.cend(), fields, auditPattern, boost::match_continuous))
    {
        return AuditLineStatus::Invalid;
    }

    time_t seconds{};
    uint32_t milliseconds{};
    uint64_t serial{};
    if (!readNumber(fields[3], seconds) || !readNumber(fields[4], milliseconds) || milliseconds > 999 || !readNumber(fields[5], serial))
    {
        return AuditLineStatus::Invalid;
    }

    if (fields[1].matched)
    {
        parsed.hostName = fields[1].str();
    }
    parsed.type = fields[2].str();
    parsed.eventId = {seconds, milliseconds, serial};
    parsed.payload.assign(fields[0].second, line.cend());
    result = std::move(parsed);
    return AuditLineStatus::Parsed;
}
} // namespace UL3::LOG2JSON::Input

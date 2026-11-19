/** @file @brief Regression coverage for auditd and syslog header parsing. */
#include "input/auditlineparser.h"
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace UANLZ::LOG2JSON::Input;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void validLines()
{
    const std::string record = "type=CWD msg=audit(1750609187.113:18446744073709551615): cwd=\"/tmp:x\"";
    AuditLine parsed;
    for (const auto &prefix : {std::string{},
                               std::string("<14>Jun 22 12:19:47 ROCKY2 audisp-syslog[673]: "),
                               std::string("<14><2001:db8::1>Jun  2 12:19:47 ROCKY2 audispd: "),
                               std::string("Mar 19 03:28:34 ROCKY2 audispd: ")})
    {
        require(parseAuditLine(prefix + record, parsed) == AuditLineStatus::Parsed, "valid framing");
        require(parsed.type == "CWD" && parsed.payload == " cwd=\"/tmp:x\"", "untouched payload");
        require(std::get<0>(parsed.eventId) == 1750609187 && std::get<1>(parsed.eventId) == 113, "timestamp");
        require(std::get<2>(parsed.eventId) == std::numeric_limits<uint64_t>::max(), "64-bit serial");
        require(parsed.hostName == (prefix.empty() ? "" : "ROCKY2"), "syslog hostname");
    }
    require(parseAuditLine("<14><192.0.2.1>Jun 22 12:19:47 host audispd: node=override " + record, parsed) == AuditLineStatus::Parsed, "forwarded node");
    require(parsed.hostName == "override" && parsed.sourceIp == "192.0.2.1", "routing fields");
    require(parseAuditLine("type=EOE msg=audit(0.0:0):", parsed) == AuditLineStatus::Parsed, "empty payload and zero identity");
    require(parsed.sourceIp.empty() && parsed.hostName.empty(), "no stale fields");
}

void invalidLines()
{
    AuditLine parsed;
    parsed.payload = "unchanged";
    for (const auto *line : {"type=CWD:",
                             "msg=audit(1.123:1):",
                             "type=CWD msg=audit(1.123:1)",
                             "type=CWD msg=audit(.123:1):",
                             "type=CWD msg=audit(1.:1):",
                             "type=CWD msg=audit(1.123:):",
                             "type=CWD msg=audit(-1.123:1):",
                             "type=CWD msg=audit(1.123:12x):",
                             "type=CWD msg=audit(1.1000:1):",
                             "type=CWD msg=audit(1.4294967296:1):",
                             "type=CWD msg=audit(9999999999999999999999.123:1):",
                             "type=CWD msg=audit(1.123:18446744073709551616):",
                             "<14>",
                             "<14><>Jun 22 12:19:47 host audispd: type=CWD msg=audit(1.123:1):",
                             "<14>Jun 22 12:19:47 host audispd type=CWD msg=audit(1.123:1):",
                             "<14>Jun 22 12:19:47 host audispd: type=CWD:",
                             "short"})
    {
        require(parseAuditLine(line, parsed) == AuditLineStatus::Invalid, line);
        require(parsed.payload == "unchanged", "failure preserves result");
    }
    require(parseAuditLine("", parsed) == AuditLineStatus::Ignored, "empty line");
    require(parseAuditLine("<14>Jun 22 12:19:47 host sshd[42]: unrelated", parsed) == AuditLineStatus::Ignored, "other program");
    const std::string longPayload(std::string::size_type{120} * 1024, 'x');
    require(parseAuditLine("type=CWD msg=audit(1.123:1):" + longPayload, parsed) == AuditLineStatus::Parsed, "large payload does not enter regex");
    require(parsed.payload == longPayload, "large payload preserved");
}
} // namespace

int main()
{
    try
    {
        validLines();
        invalidLines();
        std::cout << "PASS: raw auditd, syslog, source routing, invalid headers and numeric bounds\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

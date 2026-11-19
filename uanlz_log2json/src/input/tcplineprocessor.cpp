/**
 * @file
 * @brief Implementation for uanlz_log2json/src/input/tcplineprocessor.
 */
#include "tcplineprocessor.h"
#include "tcpserver.h"

#include "../events/events_manager.h"
#include "../globals.h"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/constants.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;
using namespace UANLZ::LOG2JSON::Input;
using namespace UANLZ::LOG2JSON;

/*
<14>Jun 22 12:19:47 ROCKY2 audisp-syslog[673]: type=CWD msg=audit(1750609187.113:113950): cwd=%22/root%22
<14><IP>Jun 22 12:19:47 ROCKY2 audisp-syslog[673]: type=CWD msg=audit(1750609187.113:113950): cwd=%22/root%22

Mar 19 03:28:34 asd audispd: node=asd.com type=PATH msg=audit(1552987714.396:292): item=0 name="/bin/cat" inode=232100
dev=fd:00 mode=0100755 ouid=0 ogid=0 rdev=00:00 obj=system_u:object_r:bin_t:s0 objtype=NORMAL cap_fp=0000000000000000
cap_fi=0000000000000000 cap_fe=0 cap_fver=0
*/

TcpLineProcessor::TcpLineProcessor(const std::shared_ptr<Mantids30::Core::Memory::Streams::StreamableObject> &socket, TcpServer &server)
    : LineRecv(socket)
    , server(server)
{
    processedLinesCount = 0;
    invalidLinesCount = 0;

    setMaxLineSize(128 * 1024);
}

TcpLineProcessor::~TcpLineProcessor() {}

json TcpLineProcessor::getStats()
{
    boost::json::object connectionStats{{"ip", remoteIp}, {"processedLinesCount", (uint64_t) processedLinesCount}, {"invalidLinesCount", (uint64_t) invalidLinesCount}};
    return connectionStats;
}

#define MODE_RSYSLOG_PURE 0
#define MODE_RSYSLOG_IP 1
#define MODE_AUDITD_PURE 2

bool TcpLineProcessor::processParsedLine(const string &line)
{
    if (line.size() < 6)
        return true;

    int inputMode = MODE_AUDITD_PURE;

    if (line.at(0) == '<')
    {
        inputMode = MODE_RSYSLOG_PURE;
        // Check if the character after the first '>' is a '<'
        size_t priorityEnd = line.find('>');
        if (priorityEnd != string::npos && priorityEnd + 1 < line.size() && line[priorityEnd + 1] == '<')
        {
            inputMode = MODE_RSYSLOG_IP;
        }
    }
    size_t readPosition = 0;

    std::string sourceIp;
    SyslogData syslogData;
    AuditdHeader auditdHeader;

    if (inputMode == MODE_AUDITD_PURE)
        syslogData.programName = "audispd";

    if (inputMode == MODE_RSYSLOG_IP || inputMode == MODE_RSYSLOG_PURE)
    {
        if (!extractIpAddress(line, inputMode, readPosition, sourceIp))
            return true;

        if (!extractSyslogData(line, inputMode, readPosition, syslogData))
            return true;
    }

    if (syslogData.programName == "audispd" || syslogData.programName == "audisp-syslog")
    {
        // Now with the rest (line+readPosition), everything inside the first : will be the auditd header.
        // and everything after it, will be the auditd data.

        if (!extractAuditdHeader(line, inputMode, readPosition, auditdHeader))
        {
            return true;
        }

        server.incrementProcessedLines();
        processedLinesCount++;
        AuditdEvents::EventsManager::insertClassContents({auditdHeader.node.empty() ? syslogData.hostName : auditdHeader.node, sourceIp.empty() ? remoteIp : sourceIp},
                                                         auditdHeader.getEventId(),
                                                         auditdHeader.type,
                                                         line.substr(readPosition));
    }

    return true;
}

void TcpLineProcessor::addInvalidLine(const std::string &line)
{
    server.incrementInvalidLines();
    invalidLinesCount++;
    LOG_APP->log1(__func__, remoteIp, Mantids30::Runtime::Logs::WARNING, "Incoming invalid line: %s", line.c_str());
}

bool TcpLineProcessor::extractIpAddress(const std::string &line, int inputMode, size_t &readPosition, std::string &sourceIp)
{
    // Calculate the IP value and displace the read position
    // The IP came in this format: <PRIVALUE><IP ADDR>
    // PRIVALUE can be anything

    if (inputMode == MODE_RSYSLOG_PURE)
    {
        // No IP address to extract, just move past the first '>'
        size_t priorityEnd = line.find('>');
        if (priorityEnd != string::npos)
        {
            readPosition = priorityEnd + 1;
        }
        else
        {
            addInvalidLine(line);
            return false;
        }
    }
    else if (inputMode == MODE_RSYSLOG_IP)
    {
        // Extract IP address between the two '<' and '>'
        size_t priorityEnd = line.find('>');
        if (priorityEnd != string::npos)
        {
            size_t addressStart = line.find('<', priorityEnd + 1);
            if (addressStart != string::npos)
            {
                size_t addressEnd = line.find('>', addressStart + 1);
                if (addressEnd != string::npos && (addressEnd - addressStart) > 1)
                {
                    sourceIp = line.substr(addressStart + 1, addressEnd - addressStart - 1);
                    readPosition = addressEnd + 1;
                }
                else
                {
                    addInvalidLine(line);
                    return false;
                }
            }
            else
            {
                addInvalidLine(line);
                return false;
            }
        }
        else
        {
            addInvalidLine(line);
            return false;
        }
    }

    return true;
}

bool TcpLineProcessor::extractSyslogData(const std::string &line, int inputMode, size_t &readPosition, SyslogData &syslogData)
{
    // Skip the first 16 characters (date part)
    readPosition += 16;

    size_t hostnameStart = readPosition;

    // Find the first space after date to get hostname
    size_t hostnameEnd = line.find(' ', hostnameStart);
    if (hostnameEnd == string::npos)
    {
        addInvalidLine(line);
        return true; // Invalid format
    }

    syslogData.hostName = line.substr(hostnameStart, hostnameEnd - hostnameStart);
    readPosition = hostnameEnd + 1;

    // Find the program name until '[' or ' '
    size_t programEnd = line.find_first_of(" [", readPosition);
    if (programEnd == string::npos)
    {
        addInvalidLine(line);
        return true; // Invalid format
    }

    syslogData.programName = line.substr(readPosition, programEnd - readPosition);

    readPosition = programEnd;

    size_t syslogHeaderEnd = line.find(":", readPosition);
    if (programEnd == string::npos)
    {
        addInvalidLine(line);
        return true; // Invalid format
    }
    readPosition = syslogHeaderEnd + 1;

    return true;
}

bool TcpLineProcessor::extractAuditdHeader(const std::string &line, int inputMode, size_t &readPosition, AuditdHeader &auditdHeader)
{
    std::string headerText;
    size_t position = readPosition;
    bool insideTimestamp = false, headerFound = false;

    while (position < line.length())
    {
        if (line[position] == '(' && !insideTimestamp)
        {
            // We found a parenthesis
            insideTimestamp = true;
        }
        else if (line[position] == ')' && insideTimestamp)
        {
            insideTimestamp = false;
        }
        else if (line[position] == ':' && !insideTimestamp)
        {
            // We found the first colon that is not inside parentheses
            headerText = line.substr(readPosition, position - readPosition);
            readPosition = position + 1;
            headerFound = true;
            break;
        }

        position++;
    }

    // If we did not find a valid header, add invalid line and return false.
    if (!headerFound)
    {
        addInvalidLine(line);
        return false;
    }

    std::vector<std::string> tokens;
    boost::split(tokens, headerText, boost::is_any_of(" "), boost::token_compress_on);
    for (const auto &token : tokens)
    {
        if (boost::starts_with(token, "node="))
        {
            auditdHeader.node = token.substr(5);
        }
        else if (boost::starts_with(token, "type="))
        {
            auditdHeader.type = token.substr(5);
        }
        else if (boost::starts_with(token, "msg=audit(") && boost::ends_with(token, ")"))
        {
            size_t position = token.find('(');
            size_t millisecondsStart = token.find('.', position + 1);
            size_t serialStart = token.find(':', millisecondsStart + 1);

            if (position == std::string::npos || millisecondsStart == std::string::npos || serialStart == std::string::npos)
            {
                addInvalidLine(line);
                return false;
            }

            // Verify that all positions are in increasing order
            if (!(position < millisecondsStart && millisecondsStart < serialStart))
            {
                addInvalidLine(line);
                return false;
            }

            auditdHeader.eventSeconds = strtol(token.substr(position + 1, millisecondsStart - position - 1).c_str(), nullptr, 10);
            auditdHeader.eventMilliseconds = strtoul(token.substr(millisecondsStart + 1, serialStart - millisecondsStart - 1).c_str(), nullptr, 10);
            auditdHeader.eventId = strtoull(token.substr(serialStart + 1, token.size() - serialStart - 2).c_str(), nullptr, 10);
        }
    }

    return true;
}

void TcpLineProcessor::setRemoteIp(const std::string &value)
{
    remoteIp = value;
}

/**
 * @file
 * @brief Implementation for ul3_log2json/src/input/tcplineprocessor.
 */
#include "tcplineprocessor.h"
#include "tcpserver.h"

#include "../events/events_manager.h"
#include "../events/events_distributionthreads.h"
#include "../globals.h"

#include "auditlineparser.h"
#include "syslogmodules.h"

using namespace std;
using namespace UL3::LOG2JSON::Input;
using namespace UL3::LOG2JSON;

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

TcpLineProcessor::~TcpLineProcessor() = default;

boost::json::object TcpLineProcessor::getStats()
{
    boost::json::object connectionStats{{"ip", remoteIp}, {"processedLinesCount", processedLinesCount.load()}, {"invalidLinesCount", invalidLinesCount.load()}};
    return connectionStats;
}

bool TcpLineProcessor::processParsedLine(const string &line)
{
    SyslogLine syslog;
    if (parseSyslogLine(line, syslog) && syslog.logger != "audispd" && syslog.logger != "audisp-syslog")
    {
        try
        {
            auto events = SyslogModules::render(syslog.logger, syslog.message);
            if (!events.empty())
            {
                server.incrementProcessedLines();
                processedLinesCount++;
            }
            for (auto &document : events)
            {
                AuditdEvents::EventDistribution::pushEvent(std::make_unique<Events::SyslogEvent>(std::move(document)));
            }
        }
        catch (const std::exception &)
        {
            // Boost.Regex may reject an excessively complex match at runtime.
            addInvalidLine(line);
        }
        return true;
    }

    AuditLine parsed;
    switch (parseAuditLine(line, parsed))
    {
    case AuditLineStatus::Ignored:
        return true;
    case AuditLineStatus::Invalid:
        addInvalidLine(line);
        return true;
    case AuditLineStatus::Parsed:
        server.incrementProcessedLines();
        processedLinesCount++;
        AuditdEvents::EventsManager::insertRecord({parsed.hostName, parsed.sourceIp.empty() ? remoteIp : parsed.sourceIp}, parsed.eventId, parsed.type, parsed.payload);
        return true;
    }
    return true;
}

void TcpLineProcessor::addInvalidLine(const std::string &line)
{
    server.incrementInvalidLines();
    invalidLinesCount++;
    LOG_APP->log1(__func__, remoteIp, Mantids30::Runtime::Logs::WARNING, "Incoming invalid line: %s", line.c_str());
}

void TcpLineProcessor::setRemoteIp(const std::string &value)
{
    remoteIp = value;
}

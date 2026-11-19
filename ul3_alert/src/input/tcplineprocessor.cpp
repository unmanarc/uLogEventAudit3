/**
 * @file
 * @brief Implementation for ul3_alert/src/input/tcplineprocessor.
 */
#include "tcplineprocessor.h"
#include "tcpserver.h"

#include "../rules/rules.h"

#include "../globals.h"

using namespace std;
using namespace UL3::JSONALERT::Input;
using namespace UL3::JSONALERT;

TcpLineProcessor::TcpLineProcessor(const std::shared_ptr<Mantids30::Core::Memory::Streams::StreamableObject> &socket, TcpServer &server)
    : LineRecv(socket)
    , server(server)
{
    processedLinesCount = 0;
    droppedElements = 0;
    setMaxLineSize(1024 * 1024); // 1Mb per line
}

TcpLineProcessor::~TcpLineProcessor() = default;

boost::json::object TcpLineProcessor::getStats()
{
    boost::json::object connectionStats{{"ip", remoteIp}, {"processedLinesCount", processedLinesCount.load()}, {"droppedElements", droppedElements.load()}};
    return connectionStats;
}

bool TcpLineProcessor::processParsedLine(const string &line)
{
    if (line.empty())
    {
        return true;
    }
    processedLinesCount++;

    if (!Filters::Rules::enqueueLine(line))
    {
        droppedElements++;
    }
    return true;
}

void TcpLineProcessor::setRemoteIp(const std::string &value)
{
    remoteIp = value;
}

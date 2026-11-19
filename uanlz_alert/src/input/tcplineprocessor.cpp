/**
 * @file
 * @brief Implementation for uanlz_alert/src/input/tcplineprocessor.
 */
#include "tcplineprocessor.h"
#include "tcpserver.h"

#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>

#include "../rules/rules.h"

#include "../globals.h"

using namespace std;
using namespace UANLZ::JSONALERT::Input;
using namespace UANLZ::JSONALERT;

TcpLineProcessor::TcpLineProcessor(const std::shared_ptr<Mantids30::Core::Memory::Streams::StreamableObject> &socket,
                                   TcpServer &server)
    : LineRecv(socket), server(server)
{
    processedLinesCount = 0;
    droppedElements = 0;
    setMaxLineSize(1024 * 1024); // 1Mb per line
}

TcpLineProcessor::~TcpLineProcessor() {}

json TcpLineProcessor::getStats()
{
    boost::json::object connectionStats{
        {"ip", remoteIp},
        {"processedLinesCount", (uint64_t)processedLinesCount},
        {"droppedElements", (uint64_t)droppedElements}};
    return connectionStats;
}

bool TcpLineProcessor::processParsedLine(const string &line)
{
    if (line.empty())
        return true;
    processedLinesCount++;

    if (!Filters::Rules::enqueueLine(line))
        droppedElements++;
    return true;
}

void TcpLineProcessor::setRemoteIp(const std::string &value)
{
    remoteIp = value;
}

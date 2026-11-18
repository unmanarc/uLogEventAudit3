#include "tcplineprocessor.h"
#include "tcpserver.h"

#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>

#include "../rules/rules.h"

#include "../globals.h"

using namespace std;
using namespace UANLZ::JSONALERT::Input;
using namespace UANLZ::JSONALERT;

TCPLineProcessor::TCPLineProcessor(const std::shared_ptr<Mantids30::Core::Memory::Streams::StreamableObject> &sock, void *server) : LineRecv(sock)
{
    processedLinesCount=0;
    this->server = server;
    droppedElements=0;
    setMaxLineSize(1024*1024); // 1Mb per line
}

TCPLineProcessor::~TCPLineProcessor()
{
}

json TCPLineProcessor::getStats()
{
    boost::json::object v{};
    v["ip"] = remoteIP;
    v["processedLinesCount"] = (uint64_t)processedLinesCount;
    v["droppedElements"] = (uint64_t)droppedElements;
    return v;
}

bool TCPLineProcessor::processParsedLine(const string &line)
{
    if (line.empty()) return true;
    processedLinesCount++;

    if (!Filters::Rules::pushElementOnEvaluationQueue(line)) droppedElements++;
    return true;
}

void TCPLineProcessor::setRemoteIP(const std::string &value)
{
    remoteIP = value;
}

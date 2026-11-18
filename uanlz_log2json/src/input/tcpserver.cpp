#include "tcpserver.h"

#include "../globals.h"
#include "../input/tcplineprocessor.h"

#include <Mantids30/Network/Sockets/socket_tcp.h>
#include <Mantids30/Network/Sockets/acceptor_multithreaded.h>

#include <boost/property_tree/ini_parser.hpp>
#include <inttypes.h>

using namespace std;
using namespace UANLZ::LOG2JSON::Input;
using namespace UANLZ::LOG2JSON;

using namespace Mantids30::Core::Memory::Streams;
using namespace Mantids30::Network::Sockets;
using namespace Mantids30::Runtime;

void logServerThr(void * obj, const std::shared_ptr<Socket_Stream> &baseClientSocket)
{
    auto *server = static_cast<TCPServer *>(obj);
    TCPLineProcessor lineServer(baseClientSocket, *server);

    string sRemotePair = baseClientSocket->getRemotePairStr();
    bool secure = baseClientSocket->isSecure();
    lineServer.setRemoteIP(sRemotePair);

    string threadName = "IN_" + sRemotePair;
#ifndef WIN32
    pthread_setname_np(pthread_self(), threadName.c_str());
#endif

    LOG_APP->log1(__func__,sRemotePair,Logs::INFO,"Receiving %srsyslog+auditd incoming connection...", secure? "secure ":"");

    server->addClient(&lineServer);
    struct ClientRegistration
    {
        TCPServer &server;
        TCPLineProcessor &processor;
        ~ClientRegistration() { server.remClient(&processor); }
    } registration{*server, lineServer};

    Parser::ParseResult err;
    lineServer.parseObject(&err);
    LOG_APP->log1(__func__,sRemotePair, err == Parser::ParseResult::SUCCEED ? Logs::INFO : Logs::WARNING,
                  "Incoming connection finished (parser status %d).", static_cast<int>(err));


}

TCPServer::TCPServer()
{
    invalidLinesCount = 0;
    processedLinesCount = 0;
}

bool TCPServer::loadConfig(const string &file)
{
    if (!access(file.c_str(),R_OK))
        boost::property_tree::ini_parser::read_ini( file.c_str(),config);
    else
    {
        LOG_APP->log0(__func__,Logs::WARNING,"Failed to load receptor configuration file %s, insufficient permissions?", file.c_str());
        return false;
    }

    listenAddr = config.get<string>("ListenAddr","127.0.0.1");
    listenPort = config.get<uint16_t>("ListenPort",10514);
    description = config.get<string>("Description", "Log receiver @" + listenAddr + ":" + to_string(listenPort));

    return true;
}

void TCPServer::startThreaded()
{
    auto tcpServer = std::make_shared<Socket_TCP>();

    if (!tcpServer->listenOn(listenPort,listenAddr.c_str(),true))
    {
        LOG_APP->log0(__func__,Logs::ERROR,"Error creating TCP LOG listener @%s:%" PRIu16,listenAddr.c_str(),listenPort);
        return;
    }

    LOG_APP->log0(__func__,Logs::WARNING,"LOG Listener running on TCP @%s:%" PRIu16 "...", listenAddr.c_str(),tcpServer->getLocalPort());

    // STREAM MANAGER:
    acceptor->addAcceptorSocket(tcpServer);
    acceptor->callbacks.onClientConnected = &logServerThr;
    acceptor->callbacks.contextOnConnect = this;
    acceptor->startInBackground();
}

json TCPServer::getStats()
{
    boost::json::object v{{"clients", boost::json::array{}}};

    v["description"] = description;
    v["processedLinesCount"] = (uint64_t)processedLinesCount;
    v["invalidLinesCount"] = (uint64_t)invalidLinesCount;

    std::lock_guard<std::mutex> lock(mt);
    for (auto i : clientProcessors)
    {
        v["clients"].as_array().emplace_back(i->getStats());
    }

    return v;
}

void TCPServer::addClient(TCPLineProcessor *clientProcessor)
{
    std::lock_guard<std::mutex> lock(mt);
    clientProcessors.insert(clientProcessor);
}

void TCPServer::remClient(TCPLineProcessor *clientProcessor)
{
    std::lock_guard<std::mutex> lock(mt);
    clientProcessors.erase(clientProcessor);
}

string TCPServer::getListenAddr() const
{
    return listenAddr;
}

uint16_t TCPServer::getListenPort() const
{
    return listenPort;
}

void TCPServer::incProcessedLines()
{
    processedLinesCount++;
}

void TCPServer::incInvalidLines()
{
    invalidLinesCount++;
}


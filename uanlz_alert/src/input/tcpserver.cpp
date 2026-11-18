#include "tcpserver.h"

#include "../globals.h"
#include "../input/tcplineprocessor.h"

#include <Mantids30/Network/Sockets/socket_tcp.h>

#include <boost/property_tree/ini_parser.hpp>
#include <inttypes.h>

using namespace std;
using namespace UANLZ::JSONALERT;
using namespace UANLZ::JSONALERT::Input;

using namespace Mantids30::Core::Memory::Streams;
using namespace Mantids30::Network::Sockets;
using namespace Mantids30::Network::Sockets;
using namespace Mantids30::Runtime;

void logServerThr(void * obj, const std::shared_ptr<Socket_Stream> &baseClientSocket)
{
    TCPServer * server = (TCPServer *)obj;
    TCPLineProcessor *  lineServer = new TCPLineProcessor(baseClientSocket,server);

    string sRemotePair = baseClientSocket->getRemotePairStr();
    bool secure = baseClientSocket->isSecure();
    lineServer->setRemoteIP(sRemotePair);

    string threadName = "IN_" + sRemotePair;
    pthread_setname_np(pthread_self(), threadName.c_str());

    LOG_APP->log1(__func__,sRemotePair,Logs::INFO,"Receiving %sJSONTCP Incoming connection...", secure? "secure " : "");

    server->addClient(lineServer);

    Parser::ParseResult err;
    lineServer->parseObject(&err);
    LOG_APP->log1(__func__,sRemotePair, err == Parser::ParseResult::SUCCEED ? Logs::INFO : Logs::WARNING,
                  "Incoming connection finished (parser status %d).", static_cast<int>(err));

    server->remClient(lineServer);

    delete lineServer;

}

TCPServer::TCPServer()
{
}

bool TCPServer::loadConfig(const json & jConfig)
{
    listenAddr = JSON_ASSTRING(jConfig,"ListenAddr","");
    listenPort = JSON_ASUINT(jConfig,"ListenPort",0);
    description = JSON_ASSTRING(jConfig,"Description","");

    LOG_APP->log0(__func__,Logs::INFO,"Configuring JSON input '%s' @%s:%" PRIu16,description.c_str(),listenAddr.c_str(),listenPort );

    return true;
}

void TCPServer::startThreaded()
{
    auto tcpListener = std::make_shared<Socket_TCP>();

    if (!tcpListener->listenOn(listenPort,listenAddr.c_str(),true))
    {
        LOG_APP->log0(__func__,Logs::ERROR,"Error creating JSON TCP listener @%s:%" PRIu16,listenAddr.c_str(),listenPort);
        return;
    }

    LOG_APP->log0(__func__,Logs::WARNING,"JSON TCP Listener listener running @%s:%" PRIu16 "...",  listenAddr.c_str(),tcpListener->getLocalPort());

    // STREAM MANAGER:
    acceptor->addAcceptorSocket(tcpListener);
    acceptor->callbacks.onClientConnected = &logServerThr;
    acceptor->callbacks.contextOnConnect = this;
    acceptor->startInBackground();
}

json TCPServer::getStats()
{
    json v;

    v["description"] = description;

    int x=0;
    mt.lock();
    for (auto i : clientProcessors)
    {
        v["clients"][x] = i->getStats();
        x++;
    }
    mt.unlock();

    return v;
}

void TCPServer::addClient(TCPLineProcessor *clientProcessor)
{
    mt.lock();
    clientProcessors.insert(clientProcessor);
    mt.unlock();
}

void TCPServer::remClient(TCPLineProcessor *clientProcessor)
{
    mt.lock();
    clientProcessors.erase(clientProcessor);
    mt.unlock();
}

string TCPServer::getListenAddr() const
{
    return listenAddr;
}

uint16_t TCPServer::getListenPort() const
{
    return listenPort;
}

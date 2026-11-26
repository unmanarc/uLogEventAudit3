/**
 * @file
 * @brief Implementation for ul3_log2json/src/input/tcpserver.
 */
#include "tcpserver.h"

#include "../globals.h"
#include "../input/tcplineprocessor.h"

#include <Mantids30/Network/Sockets/acceptor_multithreaded.h>
#include <Mantids30/Network/Sockets/socket_tcp.h>

#include <cinttypes>

using namespace std;
using namespace UL3::LOG2JSON::Input;
using namespace UL3::LOG2JSON;

using namespace Mantids30::Core::Memory::Streams;
using namespace Mantids30::Network::Sockets;
using namespace Mantids30::Runtime;

/**
 * @brief Parse one accepted Mantids30 connection and publish its live statistics.
 * @param context Borrowed TcpServer registered in the acceptor callback configuration.
 * @param clientSocket Shared accepted socket, retained by the stack parser while receiving.
 * @note The registration guard removes the parser before its stack storage is released.
 */
void processClientConnection(void *context, const std::shared_ptr<Socket_Stream> &clientSocket)
{
    auto *server = static_cast<TcpServer *>(context);
    TcpLineProcessor lineProcessor(clientSocket, *server);

    string remoteAddress = clientSocket->getRemotePairStr();
    bool secure = clientSocket->isSecure();
    lineProcessor.setRemoteIp(remoteAddress);

    string threadName = "IN_" + remoteAddress;
#ifndef WIN32
    pthread_setname_np(pthread_self(), threadName.c_str());
#endif

    LOG_APP->log1(__func__, remoteAddress, Logs::INFO, "Receiving %srSyslog+Auditd incoming connection...", secure ? "secure " : "");

    server->addClient(&lineProcessor);
    struct ClientRegistration
    {
        TcpServer &server;
        TcpLineProcessor &processor;
        /** @brief Remove the borrowed parser before its callback scope ends. */
        ~ClientRegistration() { server.removeClient(&processor); }
    } registration{*server, lineProcessor};

    Parser::ParseResult parseResult;
    lineProcessor.parseObject(&parseResult);
    LOG_APP->log1(__func__, remoteAddress, parseResult == Parser::ParseResult::SUCCEED ? Logs::INFO : Logs::WARNING, "Incoming connection finished (parser status %d).", static_cast<int>(parseResult));
}

TcpServer::TcpServer()
{
    invalidLinesCount = 0;
    processedLinesCount = 0;
}

bool TcpServer::loadConfig(const boost::json::object &configValue)
{
    config = configValue;
    listenAddress = std::string(config.at("ListenAddr").as_string());
    listenPort = boost::json::value_to<uint16_t>(config.at("ListenPort"));
    description = std::string(config.at("Description").as_string());
    return true;
}

void TcpServer::startListener()
{
    auto listenerSocket = std::make_shared<Socket_TCP>();

    if (!listenerSocket->listenOn(listenPort, listenAddress.c_str(), true))
    {
        LOG_APP->log0(__func__, Logs::ERROR, "Error creating TCP LOG listener @%s:%" PRIu16, listenAddress.c_str(), listenPort);
        return;
    }

    LOG_APP->log0(__func__, Logs::WARNING, "LOG Listener running on TCP @%s:%" PRIu16 "...", listenAddress.c_str(), listenerSocket->getLocalPort());

    // STREAM MANAGER:
    acceptor->addAcceptorSocket(listenerSocket);
    acceptor->callbacks.onClientConnected = &processClientConnection;
    acceptor->callbacks.contextOnConnect = this;
    acceptor->startInBackground();
}

boost::json::object TcpServer::getStats()
{
    boost::json::array clients;
    std::scoped_lock lock(registryMutex);
    for (auto client : clientProcessors)
    {
        clients.emplace_back(client->getStats());
    }

    return boost::json::object{
        {"clients", std::move(clients)},
        {"description", description},
        {"processedLinesCount", processedLinesCount.load()},
        {"invalidLinesCount", invalidLinesCount.load()},
    };
}

void TcpServer::addClient(TcpLineProcessor *clientProcessor)
{
    std::scoped_lock lock(registryMutex);
    clientProcessors.insert(clientProcessor);
}

void TcpServer::removeClient(TcpLineProcessor *clientProcessor)
{
    std::scoped_lock lock(registryMutex);
    clientProcessors.erase(clientProcessor);
}

string TcpServer::getListenAddress() const
{
    return listenAddress;
}

uint16_t TcpServer::getListenPort() const
{
    return listenPort;
}

void TcpServer::incrementProcessedLines()
{
    processedLinesCount++;
}

void TcpServer::incrementInvalidLines()
{
    invalidLinesCount++;
}

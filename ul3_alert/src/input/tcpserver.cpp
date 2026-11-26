/**
 * @file
 * @brief Implementation for ul3_alert/src/input/tcpserver.
 */
#include "tcpserver.h"

#include "../globals.h"
#include "../input/tcplineprocessor.h"

#include <Mantids30/Network/Sockets/socket_tcp.h>

#include <boost/property_tree/ini_parser.hpp>
#include <cinttypes>

using namespace std;
using namespace UL3::JSONALERT;
using namespace UL3::JSONALERT::Input;

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
    pthread_setname_np(pthread_self(), threadName.c_str());

    LOG_APP->log1(__func__, remoteAddress, Logs::INFO, "Receiving %sJSONTCP Incoming connection...", secure ? "secure " : "");

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

TcpServer::TcpServer() = default;

bool TcpServer::loadConfig(const boost::json::value &configValue)
{
    listenAddress = std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "ListenAddr", ""));
    listenPort = Mantids30::DataFormat::JSON::ASUINT(configValue, "ListenPort", 0);
    description = std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "Description", ""));

    LOG_APP->log0(__func__, Logs::INFO, "Configuring JSON input '%s' @%s:%" PRIu16, description.c_str(), listenAddress.c_str(), listenPort);

    return true;
}

void TcpServer::startListener()
{
    auto listenerSocket = std::make_shared<Socket_TCP>();

    // Keep the OS receive window: listenOn's third argument is a byte count.
    if (!listenerSocket->listenOn(listenPort, listenAddress.c_str()))
    {
        LOG_APP->log0(__func__, Logs::ERROR, "Error creating JSON TCP listener @%s:%" PRIu16, listenAddress.c_str(), listenPort);
        return;
    }

    LOG_APP->log0(__func__, Logs::WARNING, "JSON TCP Listener listener running @%s:%" PRIu16 "...", listenAddress.c_str(), listenerSocket->getLocalPort());

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

#include "rpcserverimpl.h"
#include "globals.h"
#include <Mantids30/Network/Sockets/socket_tls.h>
using namespace Mantids30;
using namespace UANLZ::WEB;
void RPCServerImpl::callbackOnRPCConnect(void *, const std::shared_ptr<Network::Sockets::Socket_Stream> &sock)
{
    bool ok = false;
    const auto key = sock->readStringEx<uint16_t>(&ok, 4096);
    if (!ok || key.empty())
        return;
    const auto &config = Globals::config;
    std::string service;
    if (key == config.get<std::string>("RPCServer.AlertsApiKey"))
        service = "ALERTS";
    else if (key == config.get<std::string>("RPCServer.Log2JSONApiKey"))
        service = "LOG2JSON";
    else
    {
        LOG_APP->log1(__func__, sock->getRemotePairStr(), Runtime::Logs::WARNING, "Rejected service API key");
        return;
    }
    Globals::fastRPC->processConnection(sock, service);
}
bool RPCServerImpl::createRPCListener()
{
    const auto &config = Globals::config;
    const auto alertsKey = config.get<std::string>("RPCServer.AlertsApiKey", "");
    const auto logKey = config.get<std::string>("RPCServer.Log2JSONApiKey", "");
    if (alertsKey.empty() || logKey.empty() || alertsKey == logKey || alertsKey.find("REPLACEME") == 0 || logKey.find("REPLACEME") == 0)
    {
        LOG_APP->log0(__func__, Runtime::Logs::CRITICAL, "Configure distinct RPC service API keys");
        return false;
    }
    auto socket = std::make_shared<Network::Sockets::Socket_TLS>();
    if (!socket->tlsKeys.loadPublicKeyFromPEMFile(config.get<std::string>("RPCServer.CertFile", "snakeoil.crt").c_str())
        || !socket->tlsKeys.loadPrivateKeyFromPEMFile(config.get<std::string>("RPCServer.KeyFile", "snakeoil.key").c_str())
        || !socket->listenOn(config.get<uint16_t>("RPCServer.ListenPort", 33001), config.get<std::string>("RPCServer.ListenAddr", "127.0.0.1").c_str(), !config.get<bool>("RPCServer.UseIPv6", false)))
    {
        LOG_APP->log0(__func__, Runtime::Logs::CRITICAL, "Cannot start internal TLS listener: %s", socket->getLastError().c_str());
        return false;
    }
    Globals::fastRPC = std::make_shared<Protocol::FastRPC1>();
    Globals::fastRPC->setRemoteExecutionDisconnectedTries(0);
    Globals::rpcListener = std::make_shared<Network::Sockets::Acceptors::MultiThreaded>();
    Globals::rpcListener->parameters.setMaxConcurrentClients(config.get<uint32_t>("RPCServer.MaxClients", 16));
    Globals::rpcListener->callbacks.onClientConnected = callbackOnRPCConnect;
    Globals::rpcListener->addAcceptorSocket(socket);
    Globals::rpcListener->startInBackground();
    return true;
}

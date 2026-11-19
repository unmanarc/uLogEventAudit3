/**
 * @file
 * @brief Implementation for uanlz_web/src/rpcserverimpl.
 */
#include "rpcserverimpl.h"
#include "globals.h"
#include <Mantids30/Network/Sockets/socket_tls.h>
#include <boost/algorithm/string/predicate.hpp>
using namespace Mantids30;
using namespace UANLZ::WEB;
void RpcServerImpl::onRpcConnected(void *, const std::shared_ptr<Network::Sockets::Socket_Stream> &socket)
{
    bool keyRead = false;
    const auto apiKey = socket->readStringEx<uint16_t>(&keyRead, 4096);
    if (!keyRead || apiKey.empty())
        return;
    const auto &config = Globals::config;
    std::string serviceName;
    if (apiKey == config.get<std::string>("RPCServer.AlertsApiKey"))
        serviceName = "ALERTS";
    else if (apiKey == config.get<std::string>("RPCServer.Log2JSONApiKey"))
        serviceName = "LOG2JSON";
    else
    {
        LOG_APP->log1(__func__, socket->getRemotePairStr(), Runtime::Logs::WARNING, "Rejected service API key");
        return;
    }
    Globals::fastRpc->processConnection(socket, serviceName);
}
bool RpcServerImpl::createRpcListener()
{
    const auto &config = Globals::config;
    const auto alertsKey = config.get<std::string>("RPCServer.AlertsApiKey", "");
    const auto converterKey = config.get<std::string>("RPCServer.Log2JSONApiKey", "");
    if (alertsKey.empty() || converterKey.empty() || alertsKey == converterKey || boost::starts_with(alertsKey, "REPLACEME") || boost::starts_with(converterKey, "REPLACEME"))
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
    Globals::fastRpc = std::make_shared<Protocol::FastRPC1>();
    Globals::fastRpc->setRemoteExecutionDisconnectedTries(0);
    Globals::rpcListener = std::make_shared<Network::Sockets::Acceptors::MultiThreaded>();
    Globals::rpcListener->parameters.setMaxConcurrentClients(config.get<uint32_t>("RPCServer.MaxClients", 16));
    Globals::rpcListener->callbacks.onClientConnected = onRpcConnected;
    Globals::rpcListener->addAcceptorSocket(socket);
    Globals::rpcListener->startInBackground();
    return true;
}

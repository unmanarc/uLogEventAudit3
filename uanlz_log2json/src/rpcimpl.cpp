/**
 * @file
 * @brief Implementation for uanlz_log2json/src/rpcimpl.
 */
#include "rpcimpl.h"
#include "audit_rpc.h"

#include "globals.h"

#include "output/outputs.h"
#include "input/inputs.h"
#include "events/events_manager.h"
#include "events/events_distributionthreads.h"

#include <Mantids30/Network/Sockets/socket_tls.h>
#include <Mantids30/Protocol/FastRPC1/fastrpc.h>
#include <inttypes.h>

using namespace Mantids30::Runtime;
using namespace UANLZ::LOG2JSON;

RpcImpl::RpcImpl() {}

void RpcImpl::runRpcClient()
{
    uint16_t remotePort = Globals::getMainConfig()->get<uint16_t>("RPCConnector.RemotePort", 33001);
    std::string remoteAddress = Globals::getMainConfig()->get<std::string>("RPCConnector.RemoteAddr", "127.0.0.1");
    std::string rpcApiKey =
        Globals::getMainConfig()->get<std::string>("RPCConnector.Log2JSONApiKey", "REPLACEME_XABCXAPIX_LOG2JSON");

    Mantids30::Protocol::FastRPC1 fastRpc;

    fastRpc.addMethod("stats.EventsPerHost", UANLZ::JSON::rpcMethod<&statEventsPerHost>());
    fastRpc.addMethod("stats.OutputsDistributionThreads", UANLZ::JSON::rpcMethod<&statOutputsDistributionThreads>());
    fastRpc.addMethod("stats.Outputs", UANLZ::JSON::rpcMethod<&statOutputs>());
    fastRpc.addMethod("stats.Inputs", UANLZ::JSON::rpcMethod<&statInputs>());

    for (;;)
    {
        auto rpcSocket = std::make_shared<Mantids30::Network::Sockets::Socket_TLS>();

        if (!rpcSocket->tlsKeys.loadCAFromPEMFile(
                Globals::getMainConfig()->get<std::string>("RPCConnector.CAFile", "rpcca.crt").c_str()))
        {
            LOG_APP->log0(__func__, Logs::ERROR, "Error starting RPC Connector to %s:%" PRIu16 ": %s",
                          remoteAddress.c_str(), remotePort, "Bad TLS Certificate Authority (rpcca.crt)");
            _exit(-3);
        }

        rpcSocket->tlsKeys.setValidateServerHostname(true);
        if (rpcSocket->connectTo(remoteAddress.c_str(), remotePort))
        {
            LOG_APP->log0(__func__, Logs::INFO, "Connected to @%s:%" PRIu16, remoteAddress.c_str(), remotePort);
            if (rpcSocket->writeStringEx<uint16_t>(rpcApiKey))
                fastRpc.processConnection(rpcSocket, "SERVER");
            sleep(3);
        }
        else
        {
            LOG_APP->log0(__func__, Logs::ERROR, "Error connecting to remote API Server @%s:%" PRIu16 ": %s",
                          remoteAddress.c_str(), remotePort, rpcSocket->getLastError().c_str());
            sleep(3);
        }
    }
}

json RpcImpl::statEventsPerHost(void *, const std::string &, const json &, void *, const std::string &)
{
    boost::json::object response{
        {"hosts", AuditdEvents::EventsManager::getStats()},
        {"ok", true}};
    return response;
}

json RpcImpl::statOutputsDistributionThreads(void *, const std::string &, const json &, void *, const std::string &)
{
    boost::json::object response{
        {"events", AuditdEvents::EventDistribution::getStats()},
        {"ok", true}};
    return response;
}

json RpcImpl::statOutputs(void *, const std::string &, const json &, void *, const std::string &)
{
    boost::json::object response{
        {"outputs", Output::Outputs::getStats()},
        {"ok", true}};
    return response;
}

json RpcImpl::statInputs(void *, const std::string &, const json &, void *, const std::string &)
{
    boost::json::object response{
        {"ok", true},
        {"inputs", Input::Inputs::getStats()}};
    return response;
}

/**
 * @file
 * @brief Implementation for ul3_log2json/src/rpcimpl.
 */
#include "rpcimpl.h"
#include "audit_rpc.h"

#include "globals.h"
#include "resource_sensing.h"

#include "events/events_distributionthreads.h"
#include "events/events_manager.h"
#include "input/inputs.h"
#include "input/syslogmodules.h"
#include "output/outputs.h"

#include <Mantids30/Network/Sockets/socket_tls.h>
#include <Mantids30/Protocol/FastRPC1/fastrpc.h>
#include <cinttypes>
#include <stdexcept>
#include <thread>

using namespace Mantids30::Runtime;
using namespace UL3::LOG2JSON;

namespace {
/** @brief Read endpoint definitions and the pending restart status of both endpoint files. */
template <bool inputs>
boost::json::object readEndpointConfig(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    const auto inputConfig = Input::Inputs::readConfig();
    const auto outputConfig = Output::Outputs::readConfig();
    auto response = inputs ? inputConfig : outputConfig;
    response["restartRequired"] = inputConfig.at("modified").as_bool() || outputConfig.at("modified").as_bool();
    return response;
}

/** @brief Validate and persist GUI endpoint settings for the next service restart. */
template <bool inputs>
boost::json::object writeEndpointConfig(void *, const std::string &, const boost::json::value &request, void *, const std::string &)
{
    try
    {
        const auto *definitions = request.is_object() ? request.as_object().if_contains(inputs ? "inputs" : "outputs") : nullptr;
        if (!definitions || !definitions->is_array())
        {
            throw std::invalid_argument("Expected an endpoint array");
        }
        if constexpr (inputs)
        {
            Input::Inputs::writeConfig(definitions->as_array());
        }
        else
        {
            Output::Outputs::writeConfig(definitions->as_array());
        }
        return readEndpointConfig<inputs>(nullptr, {}, {}, nullptr, {});
    }
    catch (const std::exception &error)
    {
        return {{"ok", false}, {"message", error.what()}};
    }
}

/** @brief Reply before exiting so systemd restarts the service with its saved endpoints. */
bool reloadApp(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    LOG_APP->log0(__func__, Logs::WARNING, "Service restart requested via RPC");
    std::thread([] {
        sleep(2);
        _exit(0);
    }).detach();
    return true;
}
} // namespace

RpcImpl::RpcImpl() = default;

void RpcImpl::runRpcClient()
{
    uint16_t remotePort = Globals::getMainConfig()->get<uint16_t>("RPCConnector.RemotePort", 33001);
    std::string remoteAddress = Globals::getMainConfig()->get<std::string>("RPCConnector.RemoteAddr", "127.0.0.1");
    std::string rpcApiKey = Globals::getMainConfig()->get<std::string>("RPCConnector.Log2JSONApiKey", "REPLACEME_XABCXAPIX_LOG2JSON");

    Mantids30::Protocol::FastRPC1 fastRpc;
    fastRpc.addMethod("stats.ResourceMonitor", UL3::ResourceSensing::rpcMethod());

    fastRpc.addMethod("stats.EventsPerHost", UL3::JSON::rpcMethod<&statEventsPerHost>());
    fastRpc.addMethod("stats.OutputsDistributionThreads", UL3::JSON::rpcMethod<&statOutputsDistributionThreads>());
    fastRpc.addMethod("stats.Outputs", UL3::JSON::rpcMethod<&statOutputs>());
    fastRpc.addMethod("stats.Inputs", UL3::JSON::rpcMethod<&statInputs>());
    fastRpc.addMethod("stats.ReadInputsFileConfig", UL3::JSON::rpcMethod<&readEndpointConfig<true>>());
    fastRpc.addMethod("control.WriteInputsFileConfig", UL3::JSON::rpcMethod<&writeEndpointConfig<true>>());
    fastRpc.addMethod("stats.ReadOutputsFileConfig", UL3::JSON::rpcMethod<&readEndpointConfig<false>>());
    fastRpc.addMethod("control.WriteOutputsFileConfig", UL3::JSON::rpcMethod<&writeEndpointConfig<false>>());
    fastRpc.addMethod("control.ReloadApp", UL3::JSON::rpcMethod<&reloadApp>());
    fastRpc.addMethod("stats.SyslogModules", UL3::JSON::rpcMethod<&readSyslogModules>());
    fastRpc.addMethod("control.SyslogModules", UL3::JSON::rpcMethod<&writeSyslogModules>());

    for (;;)
    {
        auto rpcSocket = std::make_shared<Mantids30::Network::Sockets::Socket_TLS>();

        if (!rpcSocket->tlsKeys.loadCAFromPEMFile(Globals::getMainConfig()->get<std::string>("RPCConnector.CAFile", "rpcca.crt")))
        {
            LOG_APP->log0(__func__, Logs::ERROR, "Error starting RPC Connector to %s:%" PRIu16 ": %s", remoteAddress.c_str(), remotePort, "Bad TLS Certificate Authority (rpcca.crt)");
            _exit(-3);
        }

        rpcSocket->tlsKeys.setValidateServerHostname(true);
        if (rpcSocket->connectTo(remoteAddress.c_str(), remotePort))
        {
            LOG_APP->log0(__func__, Logs::INFO, "Connected to @%s:%" PRIu16, remoteAddress.c_str(), remotePort);
            if (rpcSocket->writeStringEx<uint16_t>(rpcApiKey))
            {
                fastRpc.processConnection(rpcSocket, "SERVER");
            }
            sleep(3);
        }
        else
        {
            LOG_APP->log0(__func__, Logs::ERROR, "Error connecting to remote API Server @%s:%" PRIu16 ": %s", remoteAddress.c_str(), remotePort, rpcSocket->getLastError().c_str());
            sleep(3);
        }
    }
}

boost::json::object RpcImpl::statEventsPerHost(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    boost::json::object response{{"hosts", AuditdEvents::EventsManager::getStats()}, {"ok", true}};
    return response;
}

boost::json::object RpcImpl::statOutputsDistributionThreads(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    boost::json::object response{{"events", AuditdEvents::EventDistribution::getStats()}, {"ok", true}};
    return response;
}

boost::json::object RpcImpl::statOutputs(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    boost::json::object response{{"outputs", Output::Outputs::getStats()}, {"ok", true}};
    return response;
}

boost::json::object RpcImpl::statInputs(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    boost::json::object response{{"ok", true}, {"inputs", Input::Inputs::getStats()}};
    return response;
}

boost::json::object RpcImpl::readSyslogModules(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    return {{"ok", true}, {"modules", Input::SyslogModules::getConfig()}};
}

boost::json::object RpcImpl::writeSyslogModules(void *, const std::string &, const boost::json::value &input, void *, const std::string &)
{
    try
    {
        const auto *modules = input.is_object() ? input.as_object().if_contains("modules") : nullptr;
        if (!modules || !modules->is_array())
        {
            throw std::invalid_argument("Expected a modules array");
        }
        Input::SyslogModules::replace(modules->as_array());
        return {{"ok", true}};
    }
    catch (const std::exception &error)
    {
        return {{"ok", false}, {"message", error.what()}};
    }
}

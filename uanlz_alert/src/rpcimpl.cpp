/**
 * @file
 * @brief Implementation for uanlz_alert/src/rpcimpl.
 */
#include "rpcimpl.h"
#include "audit_rpc.h"
#include <inttypes.h>

#include "globals.h"

#include "input/inputs.h"
#include "rules/rules.h"

#include <Mantids30/Network/Sockets/socket_tls.h>
#include <Mantids30/Protocol/FastRPC1/fastrpc.h>

using namespace Mantids30::Runtime;
using namespace UANLZ::JSONALERT;

RpcImpl::RpcImpl() {}

void RpcImpl::runRpcClient()
{
    uint16_t remotePort = Globals::getMainConfig()->get<uint16_t>("RPCConnector.RemotePort", 33001);
    std::string remoteAddress = Globals::getMainConfig()->get<std::string>("RPCConnector.RemoteAddr", "127.0.0.1");
    std::string rpcApiKey =
        Globals::getMainConfig()->get<std::string>("RPCConnector.AlertsApiKey", "REPLACEME_XABCXAPIX_ALERT");

    Mantids30::Protocol::FastRPC1 fastRpc;

    fastRpc.addMethod("control.RulesReload", UANLZ::JSON::rpcMethod<&controlRulesReload>());
    fastRpc.addMethod("control.RulesActions", UANLZ::JSON::rpcMethod<&controlActionsReload>());
    fastRpc.addMethod("control.RemoveRule", UANLZ::JSON::rpcMethod<&controlRemoveRule>());
    fastRpc.addMethod("control.SaveRunningRules", UANLZ::JSON::rpcMethod<&controlSaveRunningRules>());
    fastRpc.addMethod("control.WriteRulesFileConfig", UANLZ::JSON::rpcMethod<&controlWriteRulesFileConfig>());
    fastRpc.addMethod("stats.ReadRunningRules", UANLZ::JSON::rpcMethod<&statReadRunningRules>());
    fastRpc.addMethod("stats.ReadRunningRule", UANLZ::JSON::rpcMethod<&statReadRunningRule>());
    fastRpc.addMethod("stats.ReadRulesFileConfig", UANLZ::JSON::rpcMethod<&statReadRulesFileConfig>());
    fastRpc.addMethod("stats.AreRulesModified", UANLZ::JSON::rpcMethod<&statAreRulesModified>());
    fastRpc.addMethod("control.AddRule", UANLZ::JSON::rpcMethod<&controlAddRule>());
    fastRpc.addMethod("control.EditRule", UANLZ::JSON::rpcMethod<&controlEditRule>());
    fastRpc.addMethod("control.RuleUp", UANLZ::JSON::rpcMethod<&controlRuleUp>());
    fastRpc.addMethod("control.RuleDown", UANLZ::JSON::rpcMethod<&controlRuleDown>());

    fastRpc.addMethod("control.WriteActionsFileConfig", UANLZ::JSON::rpcMethod<&controlWriteActionsFileConfig>());
    fastRpc.addMethod("control.AddAction", UANLZ::JSON::rpcMethod<&controlAddAction>());
    fastRpc.addMethod("control.EditAction", UANLZ::JSON::rpcMethod<&controlEditAction>());
    fastRpc.addMethod("control.RemoveAction", UANLZ::JSON::rpcMethod<&controlRemoveAction>());
    fastRpc.addMethod("control.SaveRunningActions", UANLZ::JSON::rpcMethod<&controlSaveRunningActions>());
    fastRpc.addMethod("stats.AreActionsModified", UANLZ::JSON::rpcMethod<&statAreActionsModified>());
    fastRpc.addMethod("stats.ReadRunningActions", UANLZ::JSON::rpcMethod<&statReadRunningActions>());
    fastRpc.addMethod("stats.ReadRunningAction", UANLZ::JSON::rpcMethod<&statReadRunningAction>());
    fastRpc.addMethod("stats.ReadActionsFileConfig", UANLZ::JSON::rpcMethod<&statReadActionsFileConfig>());

    fastRpc.addMethod("stats.ReadRunningInputs", UANLZ::JSON::rpcMethod<&statReadRunningInputs>());
    fastRpc.addMethod("stats.ReadInputsFileConfig", UANLZ::JSON::rpcMethod<&statReadInputsFileConfig>());
    fastRpc.addMethod("control.AddInputToFileConfig", UANLZ::JSON::rpcMethod<&controlAddInput>());
    fastRpc.addMethod("control.RemInputFromFileConfig", UANLZ::JSON::rpcMethod<&controlRemoveInput>());

    fastRpc.addMethod("stats.SystemStats", UANLZ::JSON::rpcMethod<&statSystemStats>());
    fastRpc.addMethod("control.ReloadApp", UANLZ::JSON::rpcMethod<&controlReloadApp>());

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
            LOG_APP->log0(__func__, Logs::INFO, "Connected to %s:%" PRIu16, remoteAddress.c_str(), remotePort);
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

json RpcImpl::controlWriteRulesFileConfig(void *, const std::string &, const json &configValue, void *,
                                          const std::string &)
{
    return Filters::Rules::writeRulesFileConfig(configValue);
}

json RpcImpl::statReadRulesFileConfig(void *, const std::string &, const json &, void *, const std::string &)
{
    return Filters::Rules::readRulesFileConfig();
}

json RpcImpl::statReadRunningRules(void *, const std::string &, const json &, void *, const std::string &)
{
    return Filters::Rules::getCurrentRunningRules();
}

json RpcImpl::statReadRunningRule(void *, const std::string &, const json &configValue, void *, const std::string &)
{
    return Filters::Rules::getCurrentRunningRule(Mantids30::DataFormat::JSON::ASUINT(configValue, "id", 0xFFFFFFFF));
}

json RpcImpl::statAreRulesModified(void *, const std::string &, const json &, void *, const std::string &)
{
    return Filters::Rules::getRulesModified();
}

json RpcImpl::controlRemoveRule(void *, const std::string &, const json &configValue, void *, const std::string &)
{
    return Filters::Rules::removeRule(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF));
}

json RpcImpl::controlRemoveAction(void *, const std::string &, const json &configValue, void *, const std::string &)
{
    return Filters::Rules::removeAction(std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "name", "")));
}

json RpcImpl::controlSaveRunningRules(void *, const std::string &, const json &, void *, const std::string &)
{
    return Filters::Rules::writeRulesFileConfig(Filters::Rules::getCurrentRunningRules());
}

json RpcImpl::controlSaveRunningActions(void *, const std::string &, const json &, void *, const std::string &)
{
    return Filters::Rules::writeActionsFileConfig(Filters::Rules::getCurrentRunningActions());
}

json RpcImpl::controlAddAction(void *, const std::string &, const json &configValue, void *, const std::string &)
{
    return Filters::Rules::addAction(configValue);
}

json RpcImpl::controlEditAction(void *, const std::string &, const json &configValue, void *, const std::string &)
{
    return Filters::Rules::editAction(configValue);
}

json RpcImpl::controlEditRule(void *, const std::string &, const json &configValue, void *, const std::string &)
{
    return Filters::Rules::editRule(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF),
                                    Mantids30::DataFormat::JSON::MEMBER_OR_NULL(configValue, "rule"));
}

json RpcImpl::controlAddRule(void *, const std::string &, const json &configValue, void *, const std::string &)
{
    return Filters::Rules::addRule(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF),
                                   Mantids30::DataFormat::JSON::MEMBER_OR_NULL(configValue, "rule"));
}

json RpcImpl::controlRuleUp(void *, const std::string &, const json &configValue, void *, const std::string &)
{
    return Filters::Rules::moveRuleUp(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF));
}

json RpcImpl::controlRuleDown(void *, const std::string &, const json &configValue, void *, const std::string &)
{
    return Filters::Rules::moveRuleDown(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF));
}

json RpcImpl::controlWriteActionsFileConfig(void *, const std::string &, const json &configValue, void *,
                                            const std::string &)
{
    return Filters::Rules::writeActionsFileConfig(configValue);
}

json RpcImpl::statReadActionsFileConfig(void *, const std::string &, const json &, void *, const std::string &)
{
    return Filters::Rules::readActionsFileConfig();
}

json RpcImpl::statAreActionsModified(void *, const std::string &, const json &, void *, const std::string &)
{
    return Filters::Rules::getActionsModified();
}

json RpcImpl::statReadRunningActions(void *, const std::string &, const json &, void *, const std::string &)
{
    return Filters::Rules::getCurrentRunningActions();
}

json RpcImpl::statReadRunningAction(void *, const std::string &, const json &configValue, void *, const std::string &)
{
    return Filters::Rules::getCurrentRunningAction(
        std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "name", "")));
}

json RpcImpl::controlRemoveInput(void *, const std::string &, const json &configValue, void *, const std::string &)
{
    return Input::Inputs::removeInput(std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "id", "")));
}

json RpcImpl::controlAddInput(void *, const std::string &, const json &configValue, void *, const std::string &)
{
    return Input::Inputs::addInput(configValue);
}

json RpcImpl::statReadInputsFileConfig(void *, const std::string &, const json &, void *, const std::string &)
{
    boost::json::object response{
        {"modified", Input::Inputs::getModified()},
        {"inputs", Input::Inputs::readInputs()}};
    return response;
}

json RpcImpl::statReadRunningInputs(void *, const std::string &, const json &, void *, const std::string &)
{
    return Input::Inputs::getStats();
}

/**
 * @brief Delay exit long enough to return the RPC reply before a service-manager restart.
 */
void sleepAndExit()
{
    sleep(2);
    _exit(0);
}

json RpcImpl::controlReloadApp(void *, const std::string &, const json &, void *, const std::string &)
{
    json response = true;
    LOG_APP->log0(__func__, Logs::WARNING, "Reload requested via RPC");

    std::thread(sleepAndExit).detach();
    return response;
}

json RpcImpl::statSystemStats(void *, const std::string &, const json &, void *, const std::string &)
{
    json response = Filters::Rules::getStats();
    return response;
}

json RpcImpl::controlRulesReload(void *, const std::string &, const json &, void *, const std::string &)
{
    json response = Filters::Rules::reloadRules();
    return response;
}

json RpcImpl::controlActionsReload(void *, const std::string &, const json &, void *, const std::string &)
{
    json response = Filters::Rules::reloadActions();
    return response;
}

/**
 * @file
 * @brief Implementation for ul3_alert/src/rpcimpl.
 */
#include "rpcimpl.h"
#include "audit_rpc.h"
#include <cinttypes>

#include "globals.h"

#include "input/inputs.h"
#include "rules/rules.h"

#include <Mantids30/Network/Sockets/socket_tls.h>
#include <Mantids30/Protocol/FastRPC1/fastrpc.h>

using namespace Mantids30::Runtime;
using namespace UL3::JSONALERT;

RpcImpl::RpcImpl() = default;

void RpcImpl::runRpcClient()
{
    uint16_t remotePort = Globals::getMainConfig()->get<uint16_t>("RPCConnector.RemotePort", 33001);
    std::string remoteAddress = Globals::getMainConfig()->get<std::string>("RPCConnector.RemoteAddr", "127.0.0.1");
    std::string rpcApiKey = Globals::getMainConfig()->get<std::string>("RPCConnector.AlertsApiKey", "REPLACEME_XABCXAPIX_ALERT");

    Mantids30::Protocol::FastRPC1 fastRpc;

    fastRpc.addMethod("control.RulesReload", UL3::JSON::rpcMethod<&controlRulesReload>());
    fastRpc.addMethod("control.RulesActions", UL3::JSON::rpcMethod<&controlActionsReload>());
    fastRpc.addMethod("control.RemoveRule", UL3::JSON::rpcMethod<&controlRemoveRule>());
    fastRpc.addMethod("control.SaveRunningRules", UL3::JSON::rpcMethod<&controlSaveRunningRules>());
    fastRpc.addMethod("control.WriteRulesFileConfig", UL3::JSON::rpcMethod<&controlWriteRulesFileConfig>());
    fastRpc.addMethod("stats.ReadRunningRules", UL3::JSON::rpcMethod<&statReadRunningRules>());
    fastRpc.addMethod("stats.ReadRunningRule", UL3::JSON::rpcMethod<&statReadRunningRule>());
    fastRpc.addMethod("stats.ReadRulesFileConfig", UL3::JSON::rpcMethod<&statReadRulesFileConfig>());
    fastRpc.addMethod("stats.AreRulesModified", UL3::JSON::rpcMethod<&statAreRulesModified>());
    fastRpc.addMethod("control.AddRule", UL3::JSON::rpcMethod<&controlAddRule>());
    fastRpc.addMethod("control.EditRule", UL3::JSON::rpcMethod<&controlEditRule>());
    fastRpc.addMethod("control.RuleUp", UL3::JSON::rpcMethod<&controlRuleUp>());
    fastRpc.addMethod("control.RuleDown", UL3::JSON::rpcMethod<&controlRuleDown>());

    fastRpc.addMethod("control.WriteActionsFileConfig", UL3::JSON::rpcMethod<&controlWriteActionsFileConfig>());
    fastRpc.addMethod("control.AddAction", UL3::JSON::rpcMethod<&controlAddAction>());
    fastRpc.addMethod("control.EditAction", UL3::JSON::rpcMethod<&controlEditAction>());
    fastRpc.addMethod("control.RemoveAction", UL3::JSON::rpcMethod<&controlRemoveAction>());
    fastRpc.addMethod("control.SaveRunningActions", UL3::JSON::rpcMethod<&controlSaveRunningActions>());
    fastRpc.addMethod("stats.AreActionsModified", UL3::JSON::rpcMethod<&statAreActionsModified>());
    fastRpc.addMethod("stats.ReadRunningActions", UL3::JSON::rpcMethod<&statReadRunningActions>());
    fastRpc.addMethod("stats.ReadRunningAction", UL3::JSON::rpcMethod<&statReadRunningAction>());
    fastRpc.addMethod("stats.ReadActionsFileConfig", UL3::JSON::rpcMethod<&statReadActionsFileConfig>());

    fastRpc.addMethod("stats.ReadRunningInputs", UL3::JSON::rpcMethod<&statReadRunningInputs>());
    fastRpc.addMethod("stats.ReadInputsFileConfig", UL3::JSON::rpcMethod<&statReadInputsFileConfig>());
    fastRpc.addMethod("control.AddInputToFileConfig", UL3::JSON::rpcMethod<&controlAddInput>());
    fastRpc.addMethod("control.RemInputFromFileConfig", UL3::JSON::rpcMethod<&controlRemoveInput>());

    fastRpc.addMethod("stats.SystemStats", UL3::JSON::rpcMethod<&statSystemStats>());
    fastRpc.addMethod("control.ReloadApp", UL3::JSON::rpcMethod<&controlReloadApp>());

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
            LOG_APP->log0(__func__, Logs::INFO, "Connected to %s:%" PRIu16, remoteAddress.c_str(), remotePort);
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

bool RpcImpl::controlWriteRulesFileConfig(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &)
{
    return Filters::Rules::writeRulesFileConfig(configValue);
}

boost::json::value RpcImpl::statReadRulesFileConfig(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    return Filters::Rules::readRulesFileConfig();
}

boost::json::array RpcImpl::statReadRunningRules(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    return Filters::Rules::getCurrentRunningRules();
}

boost::json::value RpcImpl::statReadRunningRule(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &)
{
    return Filters::Rules::getCurrentRunningRule(Mantids30::DataFormat::JSON::ASUINT(configValue, "id", 0xFFFFFFFF));
}

bool RpcImpl::statAreRulesModified(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    return Filters::Rules::getRulesModified();
}

bool RpcImpl::controlRemoveRule(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &)
{
    return Filters::Rules::removeRule(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF));
}

bool RpcImpl::controlRemoveAction(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &)
{
    return Filters::Rules::removeAction(std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "name", "")));
}

bool RpcImpl::controlSaveRunningRules(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    return Filters::Rules::writeRulesFileConfig(Filters::Rules::getCurrentRunningRules());
}

bool RpcImpl::controlSaveRunningActions(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    return Filters::Rules::writeActionsFileConfig(Filters::Rules::getCurrentRunningActions());
}

bool RpcImpl::controlAddAction(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &)
{
    return Filters::Rules::addAction(configValue);
}

bool RpcImpl::controlEditAction(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &)
{
    return Filters::Rules::editAction(configValue);
}

bool RpcImpl::controlEditRule(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &)
{
    return Filters::Rules::editRule(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF), Mantids30::DataFormat::JSON::MEMBER_OR_NULL(configValue, "rule"));
}

bool RpcImpl::controlAddRule(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &)
{
    return Filters::Rules::addRule(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF), Mantids30::DataFormat::JSON::MEMBER_OR_NULL(configValue, "rule"));
}

bool RpcImpl::controlRuleUp(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &)
{
    return Filters::Rules::moveRuleUp(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF));
}

bool RpcImpl::controlRuleDown(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &)
{
    return Filters::Rules::moveRuleDown(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF));
}

bool RpcImpl::controlWriteActionsFileConfig(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &)
{
    return Filters::Rules::writeActionsFileConfig(configValue);
}

boost::json::value RpcImpl::statReadActionsFileConfig(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    return Filters::Action::redact(Filters::Rules::readActionsFileConfig());
}

bool RpcImpl::statAreActionsModified(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    return Filters::Rules::getActionsModified();
}

boost::json::array RpcImpl::statReadRunningActions(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    return Filters::Action::redact(Filters::Rules::getCurrentRunningActions()).as_array();
}

boost::json::value RpcImpl::statReadRunningAction(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &)
{
    return Filters::Action::redact(Filters::Rules::getCurrentRunningAction(std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "name", ""))));
}

bool RpcImpl::controlRemoveInput(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &)
{
    return Input::Inputs::removeInput(std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "id", "")));
}

bool RpcImpl::controlAddInput(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &)
{
    return Input::Inputs::addInput(configValue);
}

boost::json::object RpcImpl::statReadInputsFileConfig(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    boost::json::object response{{"modified", Input::Inputs::getModified()}, {"inputs", Input::Inputs::readInputs()}};
    return response;
}

boost::json::array RpcImpl::statReadRunningInputs(void *, const std::string &, const boost::json::value &, void *, const std::string &)
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

bool RpcImpl::controlReloadApp(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    bool response = true;
    LOG_APP->log0(__func__, Logs::WARNING, "Reload requested via RPC");

    std::thread(sleepAndExit).detach();
    return response;
}

boost::json::object RpcImpl::statSystemStats(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    boost::json::object response = Filters::Rules::getStats();
    return response;
}

bool RpcImpl::controlRulesReload(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    bool response = Filters::Rules::reloadRules();
    return response;
}

bool RpcImpl::controlActionsReload(void *, const std::string &, const boost::json::value &, void *, const std::string &)
{
    bool response = Filters::Rules::reloadActions();
    return response;
}

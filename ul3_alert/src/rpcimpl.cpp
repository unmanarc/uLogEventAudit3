/**
 * @file
 * @brief Implementation for ul3_alert/src/rpcimpl.
 */
#include "rpcimpl.h"
#include <cinttypes>

#include "globals.h"
#include "resource_sensing.h"

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
    fastRpc.addMethod("stats.ResourceMonitor", {&UL3::ResourceSensing::rpcHistory, nullptr});

    fastRpc.addMethod("control.RulesReload", {&controlRulesReload, nullptr});
    fastRpc.addMethod("control.RulesActions", {&controlActionsReload, nullptr});
    fastRpc.addMethod("control.RemoveRule", {&controlRemoveRule, nullptr});
    fastRpc.addMethod("control.SaveRunningRules", {&controlSaveRunningRules, nullptr});
    fastRpc.addMethod("control.WriteRulesFileConfig", {&controlWriteRulesFileConfig, nullptr});
    fastRpc.addMethod("stats.ReadRunningRules", {&statReadRunningRules, nullptr});
    fastRpc.addMethod("stats.ReadRunningRule", {&statReadRunningRule, nullptr});
    fastRpc.addMethod("stats.ReadRulesFileConfig", {&statReadRulesFileConfig, nullptr});
    fastRpc.addMethod("stats.AreRulesModified", {&statAreRulesModified, nullptr});
    fastRpc.addMethod("control.AddRule", {&controlAddRule, nullptr});
    fastRpc.addMethod("control.EditRule", {&controlEditRule, nullptr});
    fastRpc.addMethod("control.RuleUp", {&controlRuleUp, nullptr});
    fastRpc.addMethod("control.RuleDown", {&controlRuleDown, nullptr});

    fastRpc.addMethod("control.WriteActionsFileConfig", {&controlWriteActionsFileConfig, nullptr});
    fastRpc.addMethod("control.AddAction", {&controlAddAction, nullptr});
    fastRpc.addMethod("control.EditAction", {&controlEditAction, nullptr});
    fastRpc.addMethod("control.RemoveAction", {&controlRemoveAction, nullptr});
    fastRpc.addMethod("control.SaveRunningActions", {&controlSaveRunningActions, nullptr});
    fastRpc.addMethod("stats.AreActionsModified", {&statAreActionsModified, nullptr});
    fastRpc.addMethod("stats.ReadRunningActions", {&statReadRunningActions, nullptr});
    fastRpc.addMethod("stats.ReadRunningAction", {&statReadRunningAction, nullptr});
    fastRpc.addMethod("stats.ReadActionsFileConfig", {&statReadActionsFileConfig, nullptr});

    fastRpc.addMethod("stats.ReadRunningInputs", {&statReadRunningInputs, nullptr});
    fastRpc.addMethod("stats.ReadInputsFileConfig", {&statReadInputsFileConfig, nullptr});
    fastRpc.addMethod("control.AddInputToFileConfig", {&controlAddInput, nullptr});
    fastRpc.addMethod("control.RemInputFromFileConfig", {&controlRemoveInput, nullptr});

    fastRpc.addMethod("stats.SystemStats", {&statSystemStats, nullptr});
    fastRpc.addMethod("control.ReloadApp", {&controlReloadApp, nullptr});

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

boost::json::value RpcImpl::controlWriteRulesFileConfig(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::writeRulesFileConfig(configValue);
}

boost::json::value RpcImpl::statReadRulesFileConfig(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::readRulesFileConfig();
}

boost::json::value RpcImpl::statReadRunningRules(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::getCurrentRunningRules();
}

boost::json::value RpcImpl::statReadRunningRule(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::getCurrentRunningRule(Mantids30::DataFormat::JSON::ASUINT(configValue, "id", 0xFFFFFFFF));
}

boost::json::value RpcImpl::statAreRulesModified(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::getRulesModified();
}

boost::json::value RpcImpl::controlRemoveRule(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::removeRule(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF));
}

boost::json::value RpcImpl::controlRemoveAction(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::removeAction(std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "name", "")));
}

boost::json::value RpcImpl::controlSaveRunningRules(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::writeRulesFileConfig(Filters::Rules::getCurrentRunningRules());
}

boost::json::value RpcImpl::controlSaveRunningActions(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::writeActionsFileConfig(Filters::Rules::getCurrentRunningActions());
}

boost::json::value RpcImpl::controlAddAction(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::addAction(configValue);
}

boost::json::value RpcImpl::controlEditAction(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::editAction(configValue);
}

boost::json::value RpcImpl::controlEditRule(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::editRule(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF), Mantids30::DataFormat::JSON::MEMBER_OR_NULL(configValue, "rule"));
}

boost::json::value RpcImpl::controlAddRule(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::addRule(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF), Mantids30::DataFormat::JSON::MEMBER_OR_NULL(configValue, "rule"));
}

boost::json::value RpcImpl::controlRuleUp(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::moveRuleUp(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF));
}

boost::json::value RpcImpl::controlRuleDown(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::moveRuleDown(Mantids30::DataFormat::JSON::ASUINT(configValue, "pos", 0xFFFFFFFF));
}

boost::json::value RpcImpl::controlWriteActionsFileConfig(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::writeActionsFileConfig(configValue);
}

boost::json::value RpcImpl::statReadActionsFileConfig(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Action::redact(Filters::Rules::readActionsFileConfig());
}

boost::json::value RpcImpl::statAreActionsModified(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Rules::getActionsModified();
}

boost::json::value RpcImpl::statReadRunningActions(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Action::redact(Filters::Rules::getCurrentRunningActions()).as_array();
}

boost::json::value RpcImpl::statReadRunningAction(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &)
{
    return Filters::Action::redact(Filters::Rules::getCurrentRunningAction(std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "name", ""))));
}

boost::json::value RpcImpl::controlRemoveInput(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &)
{
    return Input::Inputs::removeInput(std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "id", "")));
}

boost::json::value RpcImpl::controlAddInput(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &)
{
    return Input::Inputs::addInput(configValue);
}

boost::json::value RpcImpl::statReadInputsFileConfig(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &)
{
    boost::json::object response{{"modified", Input::Inputs::getModified()}, {"inputs", Input::Inputs::readInputs()}};
    return response;
}

boost::json::value RpcImpl::statReadRunningInputs(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &)
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

boost::json::value RpcImpl::controlReloadApp(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &)
{
    bool response = true;
    LOG_APP->log0(__func__, Logs::WARNING, "Reload requested via RPC");

    std::thread(sleepAndExit).detach();
    return response;
}

boost::json::value RpcImpl::statSystemStats(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &)
{
    boost::json::object response = Filters::Rules::getStats();
    return response;
}

boost::json::value RpcImpl::controlRulesReload(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &)
{
    bool response = Filters::Rules::reloadRules();
    return response;
}

boost::json::value RpcImpl::controlActionsReload(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &)
{
    bool response = Filters::Rules::reloadActions();
    return response;
}

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

RPCImpl::RPCImpl()
{
}

void RPCImpl::runRPClient()
{
    uint16_t remotePort = Globals::getConfig_main()->get<uint16_t>("RPCConnector.RemotePort",33001);
    std::string remoteAddr = Globals::getConfig_main()->get<std::string>("RPCConnector.RemoteAddr","127.0.0.1");
    std::string rpcApiKey = Globals::getConfig_main()->get<std::string>("RPCConnector.AlertsApiKey","REPLACEME_XABCXAPIX_ALERT");

    Mantids30::Protocol::FastRPC1 fastRPC;

    // RULES:
    fastRPC.addMethod( "control.RulesReload",            UANLZ::JSON::rpcMethod<&controlRulesReload>() );
    fastRPC.addMethod( "control.RulesActions",           UANLZ::JSON::rpcMethod<&controlActionsReload>() );
    fastRPC.addMethod( "control.RemoveRule",             UANLZ::JSON::rpcMethod<&controlRemoveRule>() );
    fastRPC.addMethod( "control.SaveRunningRules",       UANLZ::JSON::rpcMethod<&controlSaveRunningRules>() );
    fastRPC.addMethod( "control.WriteRulesFileConfig",   UANLZ::JSON::rpcMethod<&controlWriteRulesFileConfig>() );
    fastRPC.addMethod( "stats.ReadRunningRules",         UANLZ::JSON::rpcMethod<&statReadRunningRules>() );
    fastRPC.addMethod( "stats.ReadRunningRule",          UANLZ::JSON::rpcMethod<&statReadRunningRule>() );
    fastRPC.addMethod( "stats.ReadRulesFileConfig",      UANLZ::JSON::rpcMethod<&statReadRulesFileConfig>() );
    fastRPC.addMethod( "stats.AreRulesModified",         UANLZ::JSON::rpcMethod<&statAreRulesModified>() );
    fastRPC.addMethod( "control.AddRule",                UANLZ::JSON::rpcMethod<&controlAddRule>() );
    fastRPC.addMethod( "control.EditRule",               UANLZ::JSON::rpcMethod<&controlEditRule>() );
    fastRPC.addMethod( "control.RuleUp",                 UANLZ::JSON::rpcMethod<&controlRuleUp>() );
    fastRPC.addMethod( "control.RuleDown",               UANLZ::JSON::rpcMethod<&controlRuleDown>() );

    // ACTIONS:
    fastRPC.addMethod( "control.WriteActionsFileConfig", UANLZ::JSON::rpcMethod<&controlWriteActionsFileConfig>() );
    fastRPC.addMethod( "control.AddAction",              UANLZ::JSON::rpcMethod<&controlAddAction>() );
    fastRPC.addMethod( "control.EditAction",             UANLZ::JSON::rpcMethod<&controlEditAction>() );
    fastRPC.addMethod( "control.RemoveAction",           UANLZ::JSON::rpcMethod<&controlRemoveAction>() );
    fastRPC.addMethod( "control.SaveRunningActions",     UANLZ::JSON::rpcMethod<&controlSaveRunningActions>() );
    fastRPC.addMethod( "stats.AreActionsModified",       UANLZ::JSON::rpcMethod<&statAreActionsModified>() );
    fastRPC.addMethod( "stats.ReadRunningActions",       UANLZ::JSON::rpcMethod<&statReadRunningActions>() );
    fastRPC.addMethod( "stats.ReadRunningAction",        UANLZ::JSON::rpcMethod<&statReadRunningAction>() );
    fastRPC.addMethod( "stats.ReadActionsFileConfig",    UANLZ::JSON::rpcMethod<&statReadActionsFileConfig>() );

    // INPUTS:
    fastRPC.addMethod( "stats.ReadRunningInputs",        UANLZ::JSON::rpcMethod<&statGetRunningInputs>() );
    fastRPC.addMethod( "stats.ReadInputsFileConfig",     UANLZ::JSON::rpcMethod<&statReadInputsFileConfig>() );
    fastRPC.addMethod( "control.AddInputToFileConfig",   UANLZ::JSON::rpcMethod<&AddInputToFileConfig>() );
    fastRPC.addMethod( "control.RemInputFromFileConfig",   UANLZ::JSON::rpcMethod<&RemInputFromFileConfig>() );


    fastRPC.addMethod( "stats.SystemStats",              UANLZ::JSON::rpcMethod<&statSystemStats>() );
    fastRPC.addMethod( "control.ReloadApp",              UANLZ::JSON::rpcMethod<&controlReloadApp>() );

    for (;;)
    {
        auto sockRPCClient = std::make_shared<Mantids30::Network::Sockets::Socket_TLS>();


        // Set the SO default security level:



        if (!sockRPCClient->tlsKeys.loadCAFromPEMFile( Globals::getConfig_main()->get<std::string>("RPCConnector.CAFile","rpcca.crt").c_str() ))
        {
            LOG_APP->log0(__func__,Logs::ERROR, "Error starting RPC Connector to %s:%" PRIu16 ": %s", remoteAddr.c_str(), remotePort, "Bad TLS Certificate Authority (rpcca.crt)");
            _exit(-3);
        }

        sockRPCClient->tlsKeys.setValidateServerHostname(true);
        if ( sockRPCClient->connectTo( remoteAddr.c_str(), remotePort ) )
        {
            LOG_APP->log0(__func__,Logs::INFO,  "Connected to %s:%" PRIu16, remoteAddr.c_str(), remotePort);
            if (sockRPCClient->writeStringEx<uint16_t>(rpcApiKey))
                fastRPC.processConnection(sockRPCClient,"SERVER");
            sleep(3);
        }
        else
        {
            LOG_APP->log0(__func__,Logs::ERROR, "Error connecting to remote API Server @%s:%" PRIu16 ": %s", remoteAddr.c_str(), remotePort, sockRPCClient->getLastError().c_str());
            sleep(3);
        }
    }
}

json RPCImpl::controlWriteRulesFileConfig(void *, const std::string &, const json &jConfig, void*, const std::string &)
{
    return Filters::Rules::writeRulesFileConfig(jConfig);
}

json RPCImpl::statReadRulesFileConfig(void *, const std::string &, const json &, void*, const std::string &)
{
    return Filters::Rules::readRulesFileConfig();
}

json RPCImpl::statReadRunningRules(void *, const std::string &, const json &, void*, const std::string &)
{
    return Filters::Rules::getCurrentRunningRules();
}

json RPCImpl::statReadRunningRule(void *, const std::string &, const json &jConfig, void*, const std::string &)
{
    return Filters::Rules::getCurrentRunningRule(JSON_ASUINT(jConfig,"id",0xFFFFFFFF));
}

json RPCImpl::statAreRulesModified(void *, const std::string &, const json &, void*, const std::string &)
{
    return Filters::Rules::getRulesModified();
}

json RPCImpl::controlRemoveRule(void *, const std::string &, const json &jConfig, void*, const std::string &)
{
    return Filters::Rules::removeRule(JSON_ASUINT(jConfig,"pos",0xFFFFFFFF));
}

json RPCImpl::controlRemoveAction(void *, const std::string &, const json &jConfig, void*, const std::string &)
{
    return Filters::Rules::removeAction(JSON_ASSTRING(jConfig,"name",""));
}

json RPCImpl::controlSaveRunningRules(void *, const std::string &, const json &, void*, const std::string &)
{
    return Filters::Rules::writeRulesFileConfig(Filters::Rules::getCurrentRunningRules());
}

json RPCImpl::controlSaveRunningActions(void *, const std::string &, const json &, void*, const std::string &)
{
    return Filters::Rules::writeActionsFileConfig(Filters::Rules::getCurrentRunningActions());
}

json RPCImpl::controlAddAction(void *, const std::string &, const json &jConfig, void*, const std::string &)
{
    return Filters::Rules::addAction(jConfig);
}

json RPCImpl::controlEditAction(void *, const std::string &, const json &jConfig, void*, const std::string &)
{
    return Filters::Rules::editAction(jConfig);
}

json RPCImpl::controlEditRule(void *, const std::string &, const json &jConfig, void*, const std::string &)
{
    return Filters::Rules::editRule(JSON_ASUINT(jConfig,"pos",0xFFFFFFFF),jConfig["rule"]);
}

json RPCImpl::controlAddRule(void *, const std::string &, const json &jConfig, void*, const std::string &)
{
    return Filters::Rules::addRule(JSON_ASUINT(jConfig,"pos",0xFFFFFFFF),jConfig["rule"]);
}

json RPCImpl::controlRuleUp(void *, const std::string &, const json &jConfig, void*, const std::string &)
{
    return Filters::Rules::ruleUp(JSON_ASUINT(jConfig,"pos",0xFFFFFFFF));
}

json RPCImpl::controlRuleDown(void *, const std::string &, const json &jConfig, void*, const std::string &)
{
    return Filters::Rules::ruleDown(JSON_ASUINT(jConfig,"pos",0xFFFFFFFF));
}

json RPCImpl::controlWriteActionsFileConfig(void *, const std::string &, const json &jConfig, void*, const std::string &)
{
    return Filters::Rules::writeActionsFileConfig(jConfig);
}

json RPCImpl::statReadActionsFileConfig(void *, const std::string &, const json &, void*, const std::string &)
{
    return Filters::Rules::readActionsFileConfig();
}

json RPCImpl::statAreActionsModified(void *, const std::string &, const json &, void*, const std::string &)
{
    return Filters::Rules::getActionsModified();
}

json RPCImpl::statReadRunningActions(void *, const std::string &, const json &, void*, const std::string &)
{
    return Filters::Rules::getCurrentRunningActions();
}

json RPCImpl::statReadRunningAction(void *, const std::string &, const json &jConfig, void*, const std::string &)
{
    return Filters::Rules::getCurrentRunningAction(JSON_ASSTRING(jConfig,"name",""));
}

json RPCImpl::RemInputFromFileConfig(void *, const std::string &, const json &jConfig, void*, const std::string &)
{
    return Input::Inputs::remInput(JSON_ASSTRING(jConfig,"id",""));
}

json RPCImpl::AddInputToFileConfig(void *, const std::string &, const json &jConfig, void*, const std::string &)
{
    return Input::Inputs::addInput(jConfig);
}

json RPCImpl::statReadInputsFileConfig(void *, const std::string &, const json &, void*, const std::string &)
{
    json j;
    j["modified"] = Input::Inputs::getModified();
    j["inputs"] = Input::Inputs::readInputs();
    return j;
}

json RPCImpl::statGetRunningInputs(void *, const std::string &, const json &, void*, const std::string &)
{
    return Input::Inputs::getStats();
}

void sleepAndExit()
{
    sleep(2);
    _exit(0);
}

json RPCImpl::controlReloadApp(void *, const std::string &, const json &, void*, const std::string &)
{
    json j = true;
    LOG_APP->log0(__func__,Logs::WARNING,  "Reload requested via RPC");

    std::thread(sleepAndExit).detach();
    return j;
}

json RPCImpl::statSystemStats(void *,const std::string &, const json &, void*, const std::string &)
{
    json j = Filters::Rules::getStats();
    return j;
}

json RPCImpl::controlRulesReload(void *,const std::string &, const json &, void*, const std::string &)
{
    json j = Filters::Rules::reloadRules();
    return j;
}

json RPCImpl::controlActionsReload(void *,const std::string &, const json &, void*, const std::string &)
{
    json j = Filters::Rules::reloadActions();
    return j;
}
/*
json RPCImpl::helloWorld(void *, const json &)
{
    json hw;
    hw["MSG"] = "Hello World!";
    return hw;
}
*/

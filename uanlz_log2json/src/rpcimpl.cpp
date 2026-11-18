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

RPCImpl::RPCImpl()
{

}

void RPCImpl::runRPClient()
{
    uint16_t remotePort = Globals::getConfig_main()->get<uint16_t>("RPCConnector.RemotePort",33001);
    std::string remoteAddr = Globals::getConfig_main()->get<std::string>("RPCConnector.RemoteAddr","127.0.0.1");
    std::string rpcApiKey = Globals::getConfig_main()->get<std::string>("RPCConnector.Log2JSONApiKey","REPLACEME_XABCXAPIX_LOG2JSON");

    Mantids30::Protocol::FastRPC1 fastRPC;

    fastRPC.addMethod( "stats.EventsPerHost",              UANLZ::JSON::rpcMethod<&statEventsPerHost>() );
    fastRPC.addMethod( "stats.OutputsDistributionThreads", UANLZ::JSON::rpcMethod<&statOutputsDistributionThreads>() );
    fastRPC.addMethod( "stats.Outputs",                    UANLZ::JSON::rpcMethod<&statOutputs>() );
    fastRPC.addMethod( "stats.Inputs",                     UANLZ::JSON::rpcMethod<&statInputs>() );

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
            LOG_APP->log0(__func__,Logs::INFO,  "Connected to @%s:%" PRIu16, remoteAddr.c_str(), remotePort);
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

json RPCImpl::statEventsPerHost(void *,const std::string &, const json &, void*, const std::string &)
{
    boost::json::object j;
    j["hosts"] = AuditdEvents::Events_Manager::getJSONStats();
    j["ok"] = true;
    return j;
}

json RPCImpl::statOutputsDistributionThreads(void *,const std::string &, const json &, void*, const std::string &)
{
    boost::json::object j;
    j["events"] = AuditdEvents::Events_DistributionThreads::stats();
    j["ok"] = true;
    return j;
}

json RPCImpl::statOutputs(void *,const std::string &, const json &, void*, const std::string &)
{
    boost::json::object j;
    j["outputs"] = Output::Outputs::getStats();
    j["ok"] = true;
    return j;
}

json RPCImpl::statInputs(void *,const std::string &, const json &, void*, const std::string &)
{
    boost::json::object j;
    j["ok"] = true;
    j["inputs"] = Input::Inputs::getStats();
    return j;
}
/*
json RPCImpl::helloWorld(void *, const json &)
{
    boost::json::object hw;
    hw["MSG"] = "Hello World!";
    return hw;
}
*/

/**
 * @file
 * @brief Declarations and contracts for ul3_web/src/globals.
 */
#pragma once
#include <Mantids30/Network/Sockets/acceptor_multithreaded.h>
#include <Mantids30/Protocol/FastRPC1/fastrpc.h>
#include <Mantids30/Runtime/Logs/applog.h>
#include <Mantids30/Runtime/Logs/rpclog.h>
#include <Mantids30/Server/WebRESTFul/engine.h>
#include <boost/property_tree/ptree.hpp>
namespace UL3::WEB {
/** @brief Hold the configuration, logs and shared resources used by this service. */
struct Globals
{
    inline static boost::property_tree::ptree config;
    inline static std::shared_ptr<Mantids30::Runtime::Logs::AppLog> appLog;
    inline static std::shared_ptr<Mantids30::Runtime::Logs::RPCLog> rpcLog;
    inline static std::shared_ptr<Mantids30::Protocol::FastRPC1> fastRpc;
    inline static std::shared_ptr<Mantids30::Network::Sockets::Acceptors::MultiThreaded> rpcListener;
    inline static std::unique_ptr<Mantids30::Server::WebRESTFul::Engine> webServer;
};
} // namespace UL3::WEB
#define LOG_APP UL3::WEB::Globals::appLog
#define LOG_RPC UL3::WEB::Globals::rpcLog

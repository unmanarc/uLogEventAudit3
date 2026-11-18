/**
 * @file
 * @brief Declarations and contracts for uanlz_web/src/globals.
 */
#pragma once
#include <Mantids30/Runtime/Logs/applog.h>
#include <Mantids30/Runtime/Logs/rpclog.h>
#include <Mantids30/Protocol/FastRPC1/fastrpc.h>
#include <Mantids30/Server/WebRESTFul/engine.h>
#include <Mantids30/Network/Sockets/acceptor_multithreaded.h>
#include <boost/property_tree/ptree.hpp>
namespace UANLZ::WEB
{
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
} // namespace UANLZ::WEB
#define LOG_APP UANLZ::WEB::Globals::appLog
#define LOG_RPC UANLZ::WEB::Globals::rpcLog

/**
 * @file
 * @brief Declarations and contracts for ul3_alert/src/input/tcpserver.
 */
#pragma once

#include "tcplineprocessor.h"
#include <Mantids30/Network/Sockets/acceptor_multithreaded.h>
#include <atomic>
#include <boost/property_tree/ptree.hpp>
#include <mutex>
#include <set>

namespace UL3::JSONALERT::Input {

/** @brief Accept TCP clients through Mantids30 and expose borrowed parser statistics. */
class TcpServer
{
public:
    /**
     * @brief Create a TCP input server backed by the Mantids30 acceptor.
     */
    TcpServer();

    /**
     * @brief Load the configuration required to initialize this input or output.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    bool loadConfig(const boost::json::value &configValue);
    /**
     * @brief Bind the configured address and start the Mantids30 connection acceptor.
     */
    void startListener();

    /**
     * @brief Return a JSON snapshot of counters and current resource usage.
     */
    boost::json::object getStats();

    /**
     * @brief Register a borrowed connection parser for statistics reporting.
     * @param clientProcessor Borrowed parser; its owner must unregister it before destruction.
     */
    void addClient(TcpLineProcessor *clientProcessor);
    /**
     * @brief Unregister a borrowed parser before its lifetime ends.
     * @param clientProcessor Borrowed parser; its owner must unregister it before destruction.
     */
    void removeClient(TcpLineProcessor *clientProcessor);

    /**
     * @brief Return the configured local bind address.
     */
    [[nodiscard]] std::string getListenAddress() const;
    /**
     * @brief Return the configured local TCP port.
     */
    [[nodiscard]] uint16_t getListenPort() const;

private:
    std::shared_ptr<Mantids30::Network::Sockets::Acceptors::MultiThreaded> acceptor = std::make_shared<Mantids30::Network::Sockets::Acceptors::MultiThreaded>();
    boost::property_tree::ptree config;
    std::string listenAddress, description;
    uint16_t listenPort{};

    std::mutex registryMutex;
    std::set<TcpLineProcessor *> clientProcessors;
};

} // namespace UL3::JSONALERT::Input

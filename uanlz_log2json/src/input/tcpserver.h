/**
 * @file
 * @brief Declarations and contracts for uanlz_log2json/src/input/tcpserver.
 */
#pragma once

#include <Mantids30/Network/Sockets/acceptor_multithreaded.h>
#include "tcplineprocessor.h"
#include <atomic>
#include <set>
#include <boost/property_tree/ptree.hpp>
#include <mutex>

namespace UANLZ::LOG2JSON::Input
{

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
     * @param file Path of the INI configuration file.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    bool loadConfig(const std::string &file);
    /**
     * @brief Bind the configured address and start the Mantids30 connection acceptor.
     */
    void startListener();

    /**
     * @brief Return a JSON snapshot of counters and current resource usage.
     */
    json getStats();

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
    std::string getListenAddress() const;
    /**
     * @brief Return the configured local TCP port.
     */
    uint16_t getListenPort() const;

    /**
     * @brief Count one accepted auditd input line.
     */
    void incrementProcessedLines();
    /**
     * @brief Count one rejected auditd input line.
     */
    void incrementInvalidLines();

private:
    std::shared_ptr<Mantids30::Network::Sockets::Acceptors::MultiThreaded> acceptor =
        std::make_shared<Mantids30::Network::Sockets::Acceptors::MultiThreaded>();

    boost::property_tree::ptree config;
    std::string listenAddress, description;
    uint16_t listenPort;
    std::atomic<uint64_t> processedLinesCount, invalidLinesCount;

    std::mutex registryMutex;
    std::set<TcpLineProcessor *> clientProcessors;
};

} // namespace UANLZ::LOG2JSON::Input

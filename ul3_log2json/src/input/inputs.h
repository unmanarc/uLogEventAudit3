/**
 * @file
 * @brief Declarations and contracts for ul3_log2json/src/input/inputs.
 */
#pragma once

#include "../input/tcpserver.h"
#include <list>
#include <memory>
#include <mutex>

namespace UL3::LOG2JSON::Input {

/** @brief Own the configured TCP input servers for this service. */
class Inputs
{
public:
    /**
     * @brief Manage the configured TCP input servers.
     */
    Inputs();
    /**
     * @brief Return a JSON snapshot of counters and current resource usage.
     */
    static boost::json::array getStats();
    /**
     * @brief Load and register the configured TCP input or output definitions.
     * @param file Path of the INI configuration file.
     */
    static void loadConfig(const std::string &file);
    /**
     * @brief Start the workers for all configured inputs or outputs.
     */
    static void startThreads();

private:
    static std::list<std::unique_ptr<TcpServer>> tcpServers;
    static std::mutex registryMutex;
};

} // namespace UL3::LOG2JSON::Input

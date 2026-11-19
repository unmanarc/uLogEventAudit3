/**
 * @file
 * @brief Declarations and contracts for uanlz_log2json/src/input/inputs.
 */
#pragma once

#include <mutex>
#include <memory>
#include <list>
#include "../input/tcpserver.h"

namespace UANLZ::LOG2JSON::Input
{

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
    static json getStats();
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

} // namespace UANLZ::LOG2JSON::Input

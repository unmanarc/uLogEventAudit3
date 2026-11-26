/**
 * @file
 * @brief Declarations and contracts for ul3_log2json/src/input/inputs.
 */
#pragma once

#include "../input/tcpserver.h"
#include "../endpointconfig.h"
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
     * @brief Load and register endpoints from a validated JSON array file.
     * @param file Path of the JSON configuration file.
     */
    static void loadConfig(const std::string &file);
    /**
     * @brief Start the workers for all configured inputs or outputs.
     */
    static void startThreads();
    /** @brief Return saved endpoint definitions and pending restart status. */
    static boost::json::object readConfig();
    /** @brief Validate and save settings for the next service restart. */
    static void writeConfig(const boost::json::array &definitions);

private:
    static EndpointConfig configuration;
    static std::list<std::unique_ptr<TcpServer>> tcpServers;
    static std::mutex registryMutex;
};

} // namespace UL3::LOG2JSON::Input

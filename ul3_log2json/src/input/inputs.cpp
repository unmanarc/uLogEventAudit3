/**
 * @file
 * @brief Implementation for ul3_log2json/src/input/inputs.
 */
#include "inputs.h"
#include <stdexcept>

using namespace UL3::LOG2JSON::Input;

std::list<std::unique_ptr<TcpServer>> Inputs::tcpServers;
std::mutex Inputs::registryMutex;

UL3::LOG2JSON::EndpointConfig Inputs::configuration{true};

Inputs::Inputs() = default;

boost::json::object Inputs::readConfig()
{
    return configuration.read();
}

void Inputs::writeConfig(const boost::json::array &definitions)
{
    configuration.replace(definitions);
}

boost::json::array Inputs::getStats()
{
    boost::json::array inputStats;

    std::scoped_lock lock(registryMutex);
    for (const auto &inputServer : tcpServers)
    {
        inputStats.emplace_back(inputServer->getStats());
    }

    return inputStats;
}

void Inputs::loadConfig(const std::string &file)
{
    const auto definitions = configuration.load(file);
    std::scoped_lock lock(registryMutex);
    for (const auto &definition : definitions)
    {
        auto endpoint = std::make_unique<TcpServer>();
        if (!endpoint->loadConfig(definition.as_object()))
        {
            throw std::runtime_error("Cannot initialize TCP endpoint");
        }
        tcpServers.push_back(std::move(endpoint));
    }
}

void Inputs::startThreads()
{
    std::scoped_lock lock(registryMutex);
    for (const auto &inputServer : tcpServers)
    {
        inputServer->startListener();
    }
}

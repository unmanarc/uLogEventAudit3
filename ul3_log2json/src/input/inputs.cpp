/**
 * @file
 * @brief Implementation for ul3_log2json/src/input/inputs.
 */
#include "inputs.h"

using namespace UL3::LOG2JSON::Input;

std::list<std::unique_ptr<TcpServer>> Inputs::tcpServers;
std::mutex Inputs::registryMutex;

Inputs::Inputs() = default;

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
    auto inputServer = std::make_unique<TcpServer>();
    if (!inputServer->loadConfig(file))
    {
        return;
    }
    std::scoped_lock lock(registryMutex);
    tcpServers.push_back(std::move(inputServer));
}

void Inputs::startThreads()
{
    std::scoped_lock lock(registryMutex);
    for (const auto &inputServer : tcpServers)
    {
        inputServer->startListener();
    }
}

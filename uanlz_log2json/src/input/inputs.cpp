/**
 * @file
 * @brief Implementation for uanlz_log2json/src/input/inputs.
 */
#include "inputs.h"

using namespace UANLZ::LOG2JSON::Input;

std::list<std::unique_ptr<TcpServer>> Inputs::tcpServers;
std::mutex Inputs::registryMutex;

Inputs::Inputs() {}

boost::json::array Inputs::getStats()
{
    boost::json::array inputStats;

    std::lock_guard<std::mutex> lock(registryMutex);
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
    std::lock_guard<std::mutex> lock(registryMutex);
    tcpServers.push_back(std::move(inputServer));
}

void Inputs::startThreads()
{
    std::lock_guard<std::mutex> lock(registryMutex);
    for (const auto &inputServer : tcpServers)
    {
        inputServer->startListener();
    }
}

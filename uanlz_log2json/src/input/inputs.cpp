#include "inputs.h"

using namespace UANLZ::LOG2JSON::Input;

std::list<std::unique_ptr<TCPServer>> Inputs::tcpServers;
std::mutex Inputs::mt;

Inputs::Inputs()
{

}

json Inputs::getStats()
{
    boost::json::array v;

    std::lock_guard<std::mutex> lock(mt);
    for (const auto &i : tcpServers)
    {
        v.emplace_back(i->getStats());
    }

    return v;
}

void Inputs::loadConfig(const std::string &file)
{
    auto in = std::make_unique<TCPServer>();
    if (!in->loadConfig(file))
    {
        return;
    }
    std::lock_guard<std::mutex> lock(mt);
    tcpServers.push_back(std::move(in));
}

void Inputs::startThreads()
{
    std::lock_guard<std::mutex> lock(mt);
    for (const auto &i : tcpServers)
    {
        i->startThreaded();
    }
}

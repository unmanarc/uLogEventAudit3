#include "inputs.h"

using namespace UANLZ::LOG2JSON::Input;

std::list<TCPServer *> Inputs::tcpServers;
std::mutex Inputs::mt;

Inputs::Inputs()
{

}

json Inputs::getStats()
{
    boost::json::array v;

    mt.lock();
    for (auto i : tcpServers)
    {
        v.emplace_back(i->getStats());
    }
    mt.unlock();

    return v;
}

void Inputs::loadConfig(const std::string &file)
{
    TCPServer * in = new TCPServer();
    if (!in->loadConfig(file))
    {
        delete in;
        return;
    }
    mt.lock();
    tcpServers.push_back(in);
    mt.unlock();
}

void Inputs::startThreads()
{
    mt.lock();
    for (auto i : tcpServers)
    {
        i->startThreaded();
    }
    mt.unlock();
}

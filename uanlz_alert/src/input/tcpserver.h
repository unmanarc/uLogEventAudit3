#ifndef RECEPTORTCPSERVER_H
#define RECEPTORTCPSERVER_H

#include <Mantids30/Network/Sockets/acceptor_multithreaded.h>
#include "tcplineprocessor.h"
#include <atomic>
#include <boost/property_tree/ptree.hpp>
#include <mutex>
#include <set>

namespace UANLZ { namespace JSONALERT { namespace Input {

class TCPServer
{
public:
    TCPServer();

    bool loadConfig(const json &jConfig );
    void startThreaded();

    json getStats();

    void addClient(TCPLineProcessor * clientProcessor);
    void remClient(TCPLineProcessor * clientProcessor);

    std::string getListenAddr() const;
    uint16_t getListenPort() const;
private:
    std::shared_ptr<Mantids30::Network::Sockets::Acceptors::MultiThreaded> acceptor = std::make_shared<Mantids30::Network::Sockets::Acceptors::MultiThreaded>();
    boost::property_tree::ptree config;
    std::string listenAddr, description;
    uint16_t listenPort;

    std::mutex mt;
    std::set<TCPLineProcessor *> clientProcessors;
};

}}}

#endif // RECEPTORTCPSERVER_H

#ifndef RECEPTORTCPSERVER_H
#define RECEPTORTCPSERVER_H

#include <Mantids30/Network/Sockets/acceptor_multithreaded.h>
#include "tcplineprocessor.h"
#include <atomic>
#include <set>
#include <boost/property_tree/ptree.hpp>
#include <mutex>

namespace UANLZ { namespace LOG2JSON { namespace Input {

class TCPServer
{
public:
    TCPServer();

    bool loadConfig( const std::string & file );
    void startThreaded();

    json getStats();

    void addClient(TCPLineProcessor * clientProcessor);
    void remClient(TCPLineProcessor * clientProcessor);

    std::string getListenAddr() const;
    uint16_t getListenPort() const;

    void incProcessedLines();
    void incInvalidLines();

private:
    std::shared_ptr<Mantids30::Network::Sockets::Acceptors::MultiThreaded> acceptor = std::make_shared<Mantids30::Network::Sockets::Acceptors::MultiThreaded>();

    boost::property_tree::ptree config;
    std::string listenAddr, description;
    uint16_t listenPort;
    std::atomic<uint64_t> processedLinesCount, invalidLinesCount;

    std::mutex mt;
    std::set<TCPLineProcessor *> clientProcessors;
};

}}}

#endif // RECEPTORTCPSERVER_H

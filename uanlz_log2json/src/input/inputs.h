#ifndef INPUTS_H
#define INPUTS_H

#include <mutex>
#include <memory>
#include <list>
#include "../input/tcpserver.h"

namespace UANLZ { namespace LOG2JSON { namespace Input {

class Inputs
{
public:
    Inputs();
    static json getStats();
    static void loadConfig( const std::string & file );
    static void startThreads();

private:
    static std::list<std::unique_ptr<TCPServer>> tcpServers;
    static std::mutex mt;
};

}}}

#endif // INPUTS_H

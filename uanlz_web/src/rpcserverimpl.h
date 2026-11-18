#pragma once
#include <Mantids30/Network/Sockets/socket_stream.h>
namespace UANLZ::WEB {
class RPCServerImpl {
public:
    static bool createRPCListener();
private:
    static void callbackOnRPCConnect(void *, const std::shared_ptr<Mantids30::Network::Sockets::Socket_Stream> &sock);
};
}

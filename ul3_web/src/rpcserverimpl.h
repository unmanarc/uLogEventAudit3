/**
 * @file
 * @brief Declarations and contracts for ul3_web/src/rpcserverimpl.
 */
#pragma once
#include <Mantids30/Network/Sockets/socket_stream.h>
namespace UL3::WEB {
/** @brief Authenticate internal TLS clients and attach them to Mantids30 FastRPC1. */
class RpcServerImpl
{
public:
    /**
     * @brief Validate service API keys and start the internal Mantids30 TLS/RPC listener.
     * @return True if the listener was created and started.
     */
    static bool createRpcListener();

private:
    /**
     * @brief Authenticate the service API key and hand the accepted TLS connection to FastRPC1.
     * @param socket Shared stream owned jointly with the Mantids30 connection machinery.
     */
    static void onRpcConnected(void *, const std::shared_ptr<Mantids30::Network::Sockets::Socket_Stream> &socket);
};
} // namespace UL3::WEB

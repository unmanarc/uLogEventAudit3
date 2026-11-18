/**
 * @file
 * @brief Declarations and contracts for uanlz_log2json/src/rpcimpl.
 */
#ifndef RPCIMPL_H
#define RPCIMPL_H

#include <audit_json.h>

namespace UANLZ
{
namespace LOG2JSON
{

/** @brief Register backend RPC operations and maintain the outbound TLS connection to the web service. */
class RpcImpl
{
public:
    /**
     * @brief Expose backend controls and statistics over the outbound FastRPC1 connection.
     */
    RpcImpl();
    /**
     * @brief Register backend RPC handlers and reconnect to the TLS web service indefinitely.
     * @note Workers use the existing service-lifetime shutdown model.
     */
    static void runRpcClient();

private:
    /**
     * @brief Return per-host pending, processed and dropped event counts.
     */
    static json statEventsPerHost(void *, const std::string &, const json &, void *, const std::string &);
    /**
     * @brief Return event distribution queue and worker counters.
     */
    static json statOutputsDistributionThreads(void *, const std::string &, const json &, void *, const std::string &);
    /**
     * @brief Return configured output connection and queue statistics.
     */
    static json statOutputs(void *, const std::string &, const json &, void *, const std::string &);
    /**
     * @brief Return configured input server and client statistics.
     */
    static json statInputs(void *, const std::string &, const json &, void *, const std::string &);
};

} // namespace LOG2JSON
} // namespace UANLZ

#endif // RPCIMPL_H

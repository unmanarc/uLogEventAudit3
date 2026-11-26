/**
 * @file
 * @brief Declarations and contracts for ul3_log2json/src/rpcimpl.
 */
#pragma once

#include <audit_json.h>

namespace UL3::LOG2JSON {

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
    /** @brief Return the editable syslog module definitions. */
    static boost::json::object readSyslogModules(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /** @brief Validate, persist and activate a complete syslog module configuration. */
    static boost::json::object writeSyslogModules(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /**
     * @brief Return per-host pending, processed and dropped event counts.
     */
    static boost::json::object statEventsPerHost(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /**
     * @brief Return event distribution queue and worker counters.
     */
    static boost::json::object statOutputsDistributionThreads(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /**
     * @brief Return configured output connection and queue statistics.
     */
    static boost::json::object statOutputs(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /**
     * @brief Return configured input server and client statistics.
     */
    static boost::json::object statInputs(void *, const std::string &, const boost::json::value &, void *, const std::string &);
};

} // namespace UL3::LOG2JSON

/**
 * @file
 * @brief Declarations and contracts for ul3_log2json/src/output/output_jsontcp.
 */
#pragma once

#include "output_base.h"
#include <Mantids30/Core/Threads/safe_queue.h>
#include <Mantids30/Network/Sockets/socket_tcp.h>
#include <memory>

#include <audit_json.h>
#include <boost/property_tree/ptree.hpp>

namespace UL3::LOG2JSON::Output {

/** @brief Queue JSON events with Mantids30 and retry delivery over a TCP connection. */
class JsonTcpOutput : public OutputBase
{
public:
    /**
     * @brief Create a JSON output queue and a Mantids30 TCP connection.
     */
    JsonTcpOutput();
    /**
     * @brief Release the resources owned by this JsonTcpOutput instance.
     */
    ~JsonTcpOutput() override;

    /**
     * @brief Load the configuration required to initialize this input or output.
     * @param file Path of the INI configuration file.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    bool loadConfig(const std::string &file) override;
    /**
     * @brief Copy an event into the Mantids30 output queue or count a rejected enqueue.
     * @param eventJson Serialized event copied to the output queues.
     * @param eventId Event identity as seconds, milliseconds and a 64-bit serial number.
     */
    void logAuditEvent(const boost::json::object &eventJson, const std::tuple<time_t, uint32_t, uint64_t> &eventId) override;
    /**
     * @brief Launch the service-lifetime TCP output worker.
     * @note Workers use the existing service-lifetime shutdown model.
     */
    void startThread() override;
    /**
     * @brief Consume queued events and retry TCP delivery for the lifetime of the service.
     */
    void process() override;

    /**
     * @brief Return a JSON snapshot of counters and current resource usage.
     */
    boost::json::object getStats() override;

private:
    /**
     * @brief Create a new TCP connection, sleeping after failed attempts.
     * @return True if the new TCP connection succeeded.
     */
    bool reconnect();

    Mantids30::Core::Threads::Safe::Queue<boost::json::object> pendingEvents;

    std::string server;
    uint16_t port{};
    std::unique_ptr<Mantids30::Network::Sockets::Socket_TCP> connection;
    std::atomic<bool> connected{false};
    std::atomic<uint64_t> dropped{0}, processed{0};
    uint32_t pushTimeoutMs{};
    boost::property_tree::ptree config;
};

} // namespace UL3::LOG2JSON::Output

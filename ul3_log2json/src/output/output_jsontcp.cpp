/**
 * @file
 * @brief Implementation for ul3_log2json/src/output/output_jsontcp.
 */
#include "output_jsontcp.h"
#include "../globals.h"
#include <boost/algorithm/string/replace.hpp>
#include <cinttypes>
#include <thread>

using namespace UL3::LOG2JSON::Output;
using namespace std;

/**
 * @brief Name the output thread and enter its service-lifetime delivery loop.
 * @param output Borrowed destination that must outlive this worker.
 */
void runTcpOutput(JsonTcpOutput *output)
{
    std::string threadName = "TCP_OUTPUT";
#ifndef WIN32
    pthread_setname_np(pthread_self(), threadName.c_str());
#endif
    output->process();
}

JsonTcpOutput::JsonTcpOutput()
{
    connection = std::make_unique<Mantids30::Network::Sockets::Socket_TCP>();
}

JsonTcpOutput::~JsonTcpOutput() = default;

bool JsonTcpOutput::loadConfig(const boost::json::object &configValue)
{
    config = configValue;
    server = std::string(config.at("Host").as_string());
    port = boost::json::value_to<uint16_t>(config.at("Port"));
    description = std::string(config.at("Description").as_string());
    pendingEvents.setMaxItems(boost::json::value_to<size_t>(config.at("MaxQueuedItems")));
    pushTimeoutMs = boost::json::value_to<uint32_t>(config.at("QueuePushTimeoutInMS"));
    return true;
}

void JsonTcpOutput::logAuditEvent(const boost::json::object &eventJson, const std::tuple<time_t, uint32_t, uint64_t> &eventId)
{
    auto eventValue = std::make_shared<boost::json::object>(eventJson);
    // push, if not report and go.
    if (!pendingEvents.push(eventValue, pushTimeoutMs))
    {
        LOG_APP->log0(__func__, Mantids30::Runtime::Logs::WARNING, "Output_JSONTCP Queue full, Event %" PRIu64 ".%" PRIu32 ":%" PRIu64 " Dropped...", get<0>(eventId), get<1>(eventId), get<2>(eventId));
        dropped++;
    }
}

void JsonTcpOutput::startThread()
{
    dropped = 0;
    processed = 0;
    std::thread(runTcpOutput, this).detach();
}

void JsonTcpOutput::process()
{
    uint16_t transmissionMode = boost::json::value_to<uint16_t>(config.at("TransmissionMode"));

    // INITIAL CONNECT ATTEMPT:
    while (!reconnect())
    {
    }

    // LOOP:
    for (;;)
    {
        // Take one JSON value from the queue...
        auto eventValue = pendingEvents.pop();
        if (eventValue)
        {
            const auto payload = transmissionMode == 0 ? Mantids30::DataFormat::JSON::toString(*eventValue) + "\n" : UL3::JSON::toStyledString(*eventValue);
            // Keep the same serialized payload across reconnection attempts.
            while (!connection->writeString(payload))
            {
                // if not, try to reconnect.
                while (!reconnect())
                {
                }
            }
            processed++;
        }
    }
}

boost::json::object JsonTcpOutput::getStats()
{
    boost::json::object outputStats{{"id", server + ":" + to_string(port)},
                                    {"queue",
                                     boost::json::object{{"size", static_cast<uint64_t>(pendingEvents.size())},
                                                         {"maxItems", static_cast<uint64_t>(pendingEvents.getMaxItems())},
                                                         {"dropped", dropped.load()},
                                                         {"processed", processed.load()}}},
                                    {"connected", connected.load()}};

    return outputStats;
}

bool JsonTcpOutput::reconnect()
{
    connection = std::make_unique<Mantids30::Network::Sockets::Socket_TCP>();

    if (!connection->connectTo(server.c_str(), port, boost::json::value_to<uint32_t>(config.at("TimeoutSecs"))))
    {
        std::string msg = connection->getLastError();
        boost::replace_all(msg, "\n", ",");
        LOG_APP->log0(__func__, Mantids30::Runtime::Logs::ERROR, "JSONTCP connection error to %s:%" PRIu16 ": %s", server.c_str(), port, msg.c_str());
        connected = false;
        sleep(boost::json::value_to<uint32_t>(config.at("ReconnectSleepTimeInSecs")));
        return false;
    }
    else
    {
        LOG_APP->log0(__func__, Mantids30::Runtime::Logs::INFO, "JSONTCP connected to %s:%" PRIu16, server.c_str(), port);
        connected = true;
        return true;
    }
}

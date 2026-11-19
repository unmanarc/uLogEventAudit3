/**
 * @file
 * @brief Implementation for uanlz_log2json/src/output/output_jsontcp.
 */
#include "output_jsontcp.h"
#include "../globals.h"
#include <boost/algorithm/string/replace.hpp>
#include <cinttypes>
#include <thread>

using namespace UANLZ::LOG2JSON::Output;
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

bool JsonTcpOutput::loadConfig(const std::string &file)
{
    if (!access(file.c_str(), R_OK))
    {
        boost::property_tree::ini_parser::read_ini(file, config);
    }
    else
    {
        LOG_APP->log0(__func__, Mantids30::Runtime::Logs::ERROR, "Unable to load Output JSON TCP Configuration: %s... Invalid Permissions", file.c_str());
        return false;
    }

    server = config.get<std::string>("Host", "127.0.0.1");
    port = config.get<uint16_t>("Port", 18200);
    description = config.get<std::string>("Description", "JSON TCP Output to " + server + ":" + to_string(port));
    pendingEvents.setMaxItems(config.get<size_t>("MaxQueuedItems", 1000000));
    pushTimeoutMs = config.get<uint32_t>("QueuePushTimeoutInMS", 0);

    return true;
}

void JsonTcpOutput::logAuditEvent(const boost::json::object &eventJson, const std::tuple<time_t, uint32_t, uint64_t> &eventId)
{
    auto eventValue = std::make_unique<boost::json::object>(eventJson);
    // push, if not report and go.
    if (!pendingEvents.push(eventValue.get(), pushTimeoutMs))
    {
        LOG_APP->log0(__func__, Mantids30::Runtime::Logs::WARNING, "Output_JSONTCP Queue full, Event %" PRIu64 ".%" PRIu32 ":%" PRIu64 " Dropped...", get<0>(eventId), get<1>(eventId), get<2>(eventId));
        dropped++;
    }
    else
    {
        static_cast<void>(eventValue.release()); // Mantids30 owns the successfully queued item.
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
    uint16_t transmissionMode = config.get<uint16_t>("TransmitionMode", 0);

    // INITIAL CONNECT ATTEMPT:
    while (!reconnect())
    {
    }

    // LOOP:
    for (;;)
    {
        // Take one JSON value from the queue...
        auto eventValue = std::unique_ptr<boost::json::object>(pendingEvents.pop());
        if (eventValue)
        {
            const auto payload = transmissionMode == 0 ? Mantids30::DataFormat::JSON::toString(*eventValue) + "\n" : UANLZ::JSON::toStyledString(*eventValue);
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
    std::string server = config.get<std::string>("Host", "127.0.0.1");
    uint16_t port = config.get<uint16_t>("Port", 18200);

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

    if (!connection->connectTo(server.c_str(), port, config.get<uint32_t>("TimeoutSecs", 10)))
    {
        std::string msg = connection->getLastError();
        boost::replace_all(msg, "\n", ",");
        LOG_APP->log0(__func__, Mantids30::Runtime::Logs::ERROR, "JSONTCP connection error to %s:%" PRIu16 ": %s", server.c_str(), port, msg.c_str());
        connected = false;
        sleep(config.get<uint32_t>("ReconnectSleepTimeInSecs", 3));
        return false;
    }
    else
    {
        LOG_APP->log0(__func__, Mantids30::Runtime::Logs::INFO, "JSONTCP connected to %s:%" PRIu16, server.c_str(), port);
        connected = true;
        return true;
    }
}

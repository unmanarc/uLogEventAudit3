#include "output_jsontcp.h"
#include "../globals.h"
#include <boost/algorithm/string/replace.hpp>
#include <thread>
#include <inttypes.h>

using namespace UANLZ::LOG2JSON::Output;
using namespace std;

void tcpOutputProcessorThread( Output_JSONTCP * output )
{
    std::string threadName = "TCP_OUTPUT";
#ifndef WIN32
    pthread_setname_np(pthread_self(), threadName.c_str());
#endif
    output->process();
}

Output_JSONTCP::Output_JSONTCP() : Output_Base()
{
    connection = std::make_unique<Mantids30::Network::Sockets::Socket_TCP>();
}

Output_JSONTCP::~Output_JSONTCP()
{

}

bool Output_JSONTCP::loadConfig(const std::string &file)
{
    if (!access(file.c_str(),R_OK))
        boost::property_tree::ini_parser::read_ini( file.c_str(),config);
    else
    {
        LOG_APP->log0(__func__,Mantids30::Runtime::Logs::ERROR, "Unable to load Output JSON TCP Configuration: %s... Invalid Permissions", file.c_str());
        return false;
    }

    server = config.get<std::string>("Host","127.0.0.1");
    port = config.get<uint16_t>("Port",18200);
    description = config.get<std::string>("Description","JSON TCP Output to " + server + ":" + to_string(port));
    queueValues.setMaxItems(config.get<size_t>("MaxQueuedItems",1000000));
    push_tmout_msecs = config.get<uint32_t>("QueuePushTimeoutInMS",0);

    return true;
}


void Output_JSONTCP::logAuditEvent(const json & eventJSON, const std::tuple<time_t, uint32_t, uint64_t> &eventId)
{
    auto value = std::make_unique<json>(eventJSON);
    // push, if not report and go.
    if (!queueValues.push(value.get(),push_tmout_msecs))
    {
        LOG_APP->log0(__func__,Mantids30::Runtime::Logs::WARNING, "Output_JSONTCP Queue full, Event %" PRIu64 ".%" PRIu32 ":%" PRIu64 " Dropped...", get<0>(eventId),get<1>(eventId),get<2>(eventId));
        dropped++;
    }
    else value.release(); // Mantids30 owns the successfully queued item.
}

void Output_JSONTCP::startThread()
{
    dropped = 0;
    processed = 0;
    std::thread(tcpOutputProcessorThread,this).detach();
}

void Output_JSONTCP::process()
{
    uint16_t transmitionMode = config.get<uint16_t>("TransmitionMode",0);

    // INITIAL CONNECT ATTEMPT:
    while (!reconnect()) {}

    // LOOP:
    for (;;)
    {
        // Take one JSON value from the queue...
        auto value = std::unique_ptr<json>(queueValues.pop());
        if (value)
        {
            const auto payload = transmitionMode == 0
                ? Mantids30::DataFormat::JSON::toString(*value) + "\n"
                : UANLZ::JSON::toStyledString(*value);
            // Keep the same serialized payload across reconnection attempts.
            while (!connection->writeString(payload))
            {
                // if not, try to reconnect.
                while (!reconnect()) {}
            }
            processed++;
        }
    }
}

json Output_JSONTCP::getStats()
{
    boost::json::object v{{"queue", boost::json::object{}}};

    std::string server = config.get<std::string>("Host","127.0.0.1");
    uint16_t port = config.get<uint16_t>("Port",18200);

    v["id"] = server + ":" + to_string(port);
    v["queue"].as_object()["size"] = (uint64_t)queueValues.size();
    v["queue"].as_object()["maxItems"] = (uint64_t)queueValues.getMaxItems();
    v["queue"].as_object()["dropped"] = (uint64_t)dropped;
    v["queue"].as_object()["processed"] = (uint64_t)processed;
    v["connected"] = (bool)connected;

    return v;
}

bool Output_JSONTCP::reconnect()
{
    connection = std::make_unique<Mantids30::Network::Sockets::Socket_TCP>();


    if (!connection->connectTo(server.c_str(),
                               port,
                               config.get<uint32_t>("TimeoutSecs",10)))
    {
        std::string msg = connection->getLastError();
        boost::replace_all(msg,"\n",",");
        LOG_APP->log0(__func__,Mantids30::Runtime::Logs::ERROR, "JSONTCP connection error to %s:%" PRIu16 ": %s",  server.c_str(), port  ,msg.c_str());
        connected = false;
        sleep(config.get<uint32_t>("ReconnectSleepTimeInSecs",3));
        return false;
    }
    else
    {
        LOG_APP->log0(__func__,Mantids30::Runtime::Logs::INFO, "JSONTCP connected to %s:%" PRIu16,  server.c_str(), port);
        connected = true;
        return true;
    }
}

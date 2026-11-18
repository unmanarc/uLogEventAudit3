#ifndef OUTPUT_JSONTCP_H
#define OUTPUT_JSONTCP_H

#include "output_base.h"
#include <Mantids30/Network/Sockets/socket_tcp.h>
#include <Mantids30/Core/Threads/safe_queue.h>
#include <memory>

#include <audit_json.h>
#include <boost/property_tree/ptree.hpp>

namespace UANLZ { namespace LOG2JSON { namespace Output {

class Output_JSONTCP : public Output_Base
{
public:
    Output_JSONTCP();
    ~Output_JSONTCP() override;

    bool loadConfig(const std::string & file) override;
    void logAuditEvent(const json & eventJSON, const std::tuple<time_t, uint32_t, uint64_t> & eventId) override;
    void startThread() override;
    void process() override;

    json getStats() override;

private:
    bool reconnect();

    Mantids30::Core::Threads::Safe::Queue<json> queueValues;

    std::string server;
    uint16_t port;
    std::unique_ptr<Mantids30::Network::Sockets::Socket_TCP> connection;
    std::atomic<bool> connected{false};
    std::atomic<uint64_t> dropped{0}, processed{0};
    uint32_t push_tmout_msecs;
    boost::property_tree::ptree config;
};

}}}
#endif // OUTPUT_JSONTCP_H


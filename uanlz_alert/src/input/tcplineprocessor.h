#ifndef LINERECEPTOR_H
#define LINERECEPTOR_H

#include <audit_json.h>
#include <Mantids30/Protocol/LineRecv/linerecv.h>
#include <thread>
#include <atomic>

namespace UANLZ { namespace JSONALERT { namespace Input {

class TCPServer;

class TCPLineProcessor : public Mantids30::Protocol::LineRecv::LineRecv
{
public:
    TCPLineProcessor(const std::shared_ptr<Mantids30::Core::Memory::Streams::StreamableObject> &sock, TCPServer &server);
    ~TCPLineProcessor() override;

    json getStats();

    void setRemoteIP(const std::string &value);


protected:
    bool processParsedLine(const std::string & line) override;
private:
    std::atomic<uint64_t> processedLinesCount, droppedElements;

    TCPServer &server;
    std::string remoteIP;
};

}}}

#endif // LINERECEPTOR_H

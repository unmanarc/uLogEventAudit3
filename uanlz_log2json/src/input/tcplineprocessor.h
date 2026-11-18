#ifndef LINERECEPTOR_H
#define LINERECEPTOR_H

#include <audit_json.h>
#include <Mantids30/Protocol/LineRecv/linerecv.h>
#include <atomic>

namespace UANLZ { namespace LOG2JSON { namespace Input {

class TCPServer;

class TCPLineProcessor : public Mantids30::Protocol::LineRecv::LineRecv
{
public:
    TCPLineProcessor(const std::shared_ptr<Mantids30::Core::Memory::Streams::StreamableObject> &sock, TCPServer &server);
    ~TCPLineProcessor() override;

    void setRemoteIP(const std::string &value);

    json getStats();

protected:
    bool processParsedLine(const std::string & line) override;
private:

    struct SyslogData
    {
        std::string hostName;
        std::string programName;
    };
    struct AuditdHeader
    {
        std::string node;
        std::string type;


        std::tuple<time_t, uint32_t, uint64_t> getEventID()
        {
            return std::make_tuple(auditdEventTime, msecs, eventId);
        }

        time_t auditdEventTime;
        uint32_t msecs;
        uint64_t eventId;

    };
    void addInvalidLine(const std::string & line);

    bool extractIPAddress(const std::string &line, int mode, size_t &readPosition, std::string &ip);
    bool extractSyslogData(const std::string &line, int mode, size_t & readPosition, SyslogData & syslogData);
    bool extractAuditdHeader(const std::string &line, int mode, size_t & readPosition, AuditdHeader & auditdHeader);

    std::atomic<uint64_t> processedLinesCount, invalidLinesCount;


    TCPServer &server;
    std::string remoteIP;
};

}}}

#endif // LINERECEPTOR_H

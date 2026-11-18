/**
 * @file
 * @brief Declarations and contracts for uanlz_log2json/src/input/tcplineprocessor.
 */
#ifndef LINERECEPTOR_H
#define LINERECEPTOR_H

#include <audit_json.h>
#include <Mantids30/Protocol/LineRecv/linerecv.h>
#include <atomic>

namespace UANLZ
{
namespace LOG2JSON
{
namespace Input
{

class TcpServer;

/** @brief Translate Mantids30 line callbacks into audit records or queued JSON events. */
class TcpLineProcessor : public Mantids30::Protocol::LineRecv::LineRecv
{
public:
    /**
     * @brief Create a line parser for a connection whose server outlives this parser.
     * @param socket Shared stream owned jointly with the Mantids30 connection machinery.
     * @param server Owning input server, which must outlive this parser.
     */
    TcpLineProcessor(const std::shared_ptr<Mantids30::Core::Memory::Streams::StreamableObject> &socket,
                     TcpServer &server);
    /**
     * @brief Release the resources owned by this TcpLineProcessor instance.
     */
    ~TcpLineProcessor() override;

    /**
     * @brief Set the peer address displayed in connection statistics and log entries.
     * @param value Peer address used for statistics and log messages.
     */
    void setRemoteIp(const std::string &value);

    /**
     * @brief Return a JSON snapshot of counters and current resource usage.
     */
    json getStats();

protected:
    /**
     * @brief Process a line supplied by Mantids30 and keep receiving on the connection.
     * @param line Received line without its line delimiter.
     * @return True to keep the Mantids30 line parser receiving more input.
     */
    bool processParsedLine(const std::string &line) override;

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

        /**
         * @brief Return the audit timestamp, milliseconds and 64-bit serial number.
         */
        std::tuple<time_t, uint32_t, uint64_t> getEventId()
        {
            return std::make_tuple(eventSeconds, eventMilliseconds, eventId);
        }

        time_t eventSeconds;
        uint32_t eventMilliseconds;
        uint64_t eventId;
    };
    /**
     * @brief Count and log a line whose auditd or syslog framing cannot be parsed.
     * @param line Received line without its line delimiter.
     */
    void addInvalidLine(const std::string &line);

    /**
     * @brief Advance past the syslog priority and extract an optional forwarded source address.
     * @param sourceIp Output source address from the optional forwarded prefix.
     * @param line Received line without its line delimiter.
     * @param inputMode Framing mode selected for raw auditd or forwarded syslog input.
     * @param readPosition Input/output offset advanced past the decoded prefix.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    bool extractIpAddress(const std::string &line, int inputMode, size_t &readPosition, std::string &sourceIp);
    /**
     * @brief Extract the syslog hostname and program and advance to the auditd payload.
     * @param line Received line without its line delimiter.
     * @param inputMode Framing mode selected for raw auditd or forwarded syslog input.
     * @param readPosition Input/output offset advanced past the decoded prefix.
     * @param syslogData Output hostname and program extracted from the syslog prefix.
     * @return True after attempting decoding; malformed fields are reported through addInvalidLine().
     */
    bool extractSyslogData(const std::string &line, int inputMode, size_t &readPosition, SyslogData &syslogData);
    /**
     * @brief Separate the auditd header from record data and decode its event identifier.
     * @param line Received line without its line delimiter.
     * @param inputMode Framing mode selected for raw auditd or forwarded syslog input.
     * @param readPosition Input/output offset advanced past the decoded prefix.
     * @param auditdHeader Output record type, optional node and event identifier.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    bool extractAuditdHeader(const std::string &line, int inputMode, size_t &readPosition, AuditdHeader &auditdHeader);

    std::atomic<uint64_t> processedLinesCount, invalidLinesCount;

    TcpServer &server;
    std::string remoteIp;
};

} // namespace Input
} // namespace LOG2JSON
} // namespace UANLZ

#endif // LINERECEPTOR_H

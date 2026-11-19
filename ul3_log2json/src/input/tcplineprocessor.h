/**
 * @file
 * @brief Declarations and contracts for ul3_log2json/src/input/tcplineprocessor.
 */
#pragma once

#include <Mantids30/Protocol/LineRecv/linerecv.h>
#include <atomic>
#include <audit_json.h>

namespace UL3::LOG2JSON::Input {

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
    TcpLineProcessor(const std::shared_ptr<Mantids30::Core::Memory::Streams::StreamableObject> &socket, TcpServer &server);
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
    boost::json::object getStats();

protected:
    /**
     * @brief Process a line supplied by Mantids30 and keep receiving on the connection.
     * @param line Received line without its line delimiter.
     * @return True to keep the Mantids30 line parser receiving more input.
     */
    bool processParsedLine(const std::string &line) override;

private:
    /**
     * @brief Count and log a line whose auditd or syslog framing cannot be parsed.
     * @param line Received line without its line delimiter.
     */
    void addInvalidLine(const std::string &line);

    std::atomic<uint64_t> processedLinesCount{}, invalidLinesCount{};

    TcpServer &server;
    std::string remoteIp;
};

} // namespace UL3::LOG2JSON::Input

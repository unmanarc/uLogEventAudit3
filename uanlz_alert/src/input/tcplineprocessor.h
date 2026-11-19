/**
 * @file
 * @brief Declarations and contracts for uanlz_alert/src/input/tcplineprocessor.
 */
#pragma once

#include <Mantids30/Protocol/LineRecv/linerecv.h>
#include <atomic>
#include <audit_json.h>
#include <thread>

namespace UANLZ::JSONALERT::Input {

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
     * @brief Return a JSON snapshot of counters and current resource usage.
     */
    json getStats();

    /**
     * @brief Set the peer address displayed in connection statistics and log entries.
     * @param value Peer address used for statistics and log messages.
     */
    void setRemoteIp(const std::string &value);

protected:
    /**
     * @brief Process a line supplied by Mantids30 and keep receiving on the connection.
     * @param line Received line without its line delimiter.
     * @return True to keep the Mantids30 line parser receiving more input.
     */
    bool processParsedLine(const std::string &line) override;

private:
    std::atomic<uint64_t> processedLinesCount, droppedElements;

    TcpServer &server;
    std::string remoteIp;
};

} // namespace UANLZ::JSONALERT::Input

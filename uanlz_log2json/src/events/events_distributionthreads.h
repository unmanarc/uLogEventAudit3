/**
 * @file
 * @brief Declarations and contracts for uanlz_log2json/src/events/events_distributionthreads.
 */
#pragma once

#include <Mantids30/Core/Threads/safe_queue.h>
#include <memory>

#include "../events/audit_event.h"

namespace UANLZ::LOG2JSON::AuditdEvents
{
/** @brief Distribute owned audit events through the Mantids30 queue to output workers. */
class EventDistribution
{
public:
    /**
     * @brief Provide access to the shared event distribution workers.
     */
    EventDistribution();

    /**
     * @brief Return a JSON snapshot of counters and current resource usage.
     */
    static json getStats();

    /**
     * @brief Set the maximum number of events admitted by the Mantids30 queue.
     * @param queueCapacity Maximum number of pending items admitted by the Mantids30 queue.
     */
    static void setQueueSize(const size_t &queueCapacity);
    /**
     * @brief Initialize counters and launch the event consumers and interval publisher.
     * @note Workers use the existing service-lifetime shutdown model.
     * @param threads Number of consumer workers to start.
     */
    static void startDistributionThreads(const size_t &threads);

    /**
     * @brief Transfer an event to Mantids30, destroying it locally if the queue rejects it.
     * @param event Unique owner consumed by this call, including on queue rejection.
     * @return True if Mantids30 accepted ownership; false if the event was dropped.
     */
    static bool pushAuditEvent(std::unique_ptr<AuditEvent> event);
    /**
     * @brief Wait for a queued event and adopt its ownership from Mantids30.
     * @param popTimeoutMs Maximum wait in milliseconds for a queued event.
     * @return Unique owner of the popped event, or nullptr on timeout.
     */
    static std::unique_ptr<AuditEvent> popAuditEvent(const uint32_t &popTimeoutMs);

    /**
     * @brief Count one event delivered to the configured outputs.
     */
    static void incrementProcessedEvents();
    /**
     * @brief Publish the processed-event interval count using an atomic exchange.
     */
    static void publishIntervalCounters();

private:
    static std::atomic<uint64_t> eventsProcessed, eventsProcessedLastTenSeconds, eventsProcessedThisPeriod,
        eventsDropped;
    static Mantids30::Core::Threads::Safe::Queue<AuditEvent> eventsQueue;
    static uint32_t pushTimeoutMs;
};

} // namespace UANLZ::LOG2JSON::AuditdEvents

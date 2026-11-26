/**
 * @file
 * @brief Implementation for ul3_log2json/src/events/events_distributionthreads.
 */
#include "events_distributionthreads.h"

#include <thread>

#include "../globals.h"
#include "../output/outputs.h"

using namespace std;
using namespace UL3::LOG2JSON::AuditdEvents;
using namespace UL3::LOG2JSON;

// Shared queue used by producers and distribution workers.
Mantids30::Core::Threads::Safe::Queue<Events::Event> EventDistribution::eventsQueue;
uint32_t EventDistribution::pushTimeoutMs = 0;

std::atomic<uint64_t> EventDistribution::eventsProcessed;
std::atomic<uint64_t> EventDistribution::eventsProcessedLastTenSeconds;
std::atomic<uint64_t> EventDistribution::eventsProcessedThisPeriod;
std::atomic<uint64_t> EventDistribution::eventsDropped;

/**
 * @brief Parse queued events and dispatch their JSON to every output.
 * @param workerId Worker number used to identify timeout diagnostics.
 * @note A queue timeout emits the existing noEventsAlert heartbeat payload.
 */
void runEventDistribution(int workerId)
{
    std::string threadName = "EVENTS_DEQUEUE";
#ifndef WIN32
    pthread_setname_np(pthread_self(), threadName.c_str());
#endif

    for (;;)
    {
        auto event = EventDistribution::popEvent(Globals::getMainConfig()->get<uint32_t>("Processor.EventsFlushTimeoutInMSecs", 10000));

        if (event)
        {
            event->process();
            boost::json::object eventJson = event->getJson();

            Output::Outputs::dispatchEvent(eventJson, event->getEventId());

            EventDistribution::incrementProcessedEvents();
        }
        else
        {
            LOG_APP->log0(__func__, Mantids30::Runtime::Logs::DEBUG, "No events so far for output thread #%d, triggering no event alert JSON...", workerId);
            boost::json::object idleEventJson{{"noEventsAlert", true}};
            Output::Outputs::dispatchEvent(idleEventJson, std::make_tuple<time_t, uint32_t, uint64_t>(0, 0, 0));
        }
    }
}

/**
 * @brief Publish the processed-event count every ten seconds for the service lifetime.
 */
void runDistributionCounters()
{
    std::string threadName = "EVENTS_COUNTER";
#ifndef WIN32
    pthread_setname_np(pthread_self(), threadName.c_str());
#endif

    for (;;)
    {
        EventDistribution::publishIntervalCounters();
        sleep(10);
    }
}

EventDistribution::EventDistribution() = default;

boost::json::object EventDistribution::getStats()
{
    boost::json::object distributionStats{{"eventsQueue",
                                           boost::json::object{{"maxItems", static_cast<uint64_t>(eventsQueue.getMaxItems())},
                                                               {"curSize", static_cast<uint64_t>(eventsQueue.size())},
                                                               {"dropped", eventsDropped.load()}}},
                                          {"eventsProcessed", boost::json::object{{"last10s", eventsProcessedLastTenSeconds.load()}, {"total", eventsProcessed.load()}}}};

    return distributionStats;
}

void EventDistribution::setQueueSize(const size_t &queueCapacity)
{
    eventsQueue.setMaxItems(queueCapacity);
}

void EventDistribution::startDistributionThreads(const size_t &threads)
{
    pushTimeoutMs = Globals::getMainConfig()->get<uint32_t>("Processor.QueuePushTimeoutInMS", 0);
    eventsProcessed = 0;
    eventsDropped = 0;
    eventsProcessedLastTenSeconds = 0;
    eventsProcessedThisPeriod = 0;

    std::thread(runDistributionCounters).detach();

    for (size_t workerIndex = 1; workerIndex <= threads; workerIndex++)
    {
        std::thread workerThread(runEventDistribution, workerIndex);
        workerThread.detach();
    }
}

bool EventDistribution::pushEvent(std::unique_ptr<Events::Event> event)
{
    bool queued = eventsQueue.push(std::shared_ptr<Events::Event>(std::move(event)), pushTimeoutMs);
    if (!queued)
    {
        eventsDropped++;
    }
    return queued;
}

std::shared_ptr<Events::Event> EventDistribution::popEvent(const uint32_t &popTimeoutMs)
{
    return eventsQueue.pop(popTimeoutMs);
}

void EventDistribution::incrementProcessedEvents()
{
    eventsProcessed++;
    eventsProcessedThisPeriod++;
}

void EventDistribution::publishIntervalCounters()
{
    eventsProcessedLastTenSeconds = eventsProcessedThisPeriod.exchange(0);
}

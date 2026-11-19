/**
 * @file
 * @brief Implementation for uanlz_log2json/src/events/events_manager.
 */
#include "events_manager.h"
#include "../globals.h"

#include <mutex>
#include <thread>

using namespace UANLZ::LOG2JSON::AuditdEvents;

EventsManager::HostMap EventsManager::hostsById;
std::mutex EventsManager::hostsMutex;

/**
 * @brief Name the garbage collector thread and enter the host expiration loop.
 * @note Runs for the lifetime of the service.
 */
void runGarbageCollector()
{
    std::string threadName = "EVENTS_GC";
#ifndef WIN32
    pthread_setname_np(pthread_self(), threadName.c_str());
#endif
    EventsManager::collectExpiredEvents();
}

EventsManager::EventsManager() {}

json EventsManager::getStats()
{
    boost::json::array hostStats;
    for (const auto &[hostId, hostState] : snapshotHosts())
    {
        boost::json::object hostJson{{"host", hostId.hostname},
                                     {"ip", hostId.ipAddress},
                                     {"events",
                                      boost::json::object{{"pending", (uint64_t) hostState->getPendingEventsCount()},
                                                          {"processed", (uint64_t) hostState->getProcessedEventsCount()},
                                                          {"dropped", (uint64_t) hostState->getDroppedEventsCount()}}}};
        hostStats.emplace_back(std::move(hostJson));
    }
    return hostStats;
}

void EventsManager::collectExpiredEvents()
{
    for (;;)
    {
        uint64_t maxEventAgeSeconds = Globals::getMainConfig()->get<uint64_t>("GC.AuditMaxAgeInSecs", 10);
        uint64_t collectionIntervalSeconds = Globals::getMainConfig()->get<uint64_t>("GC.GCPeriodInSecs", 5);

        for (const auto &entry : snapshotHosts())
            entry.second->flushExpiredEvents(maxEventAgeSeconds);
        sleep(collectionIntervalSeconds);
    }
}

void EventsManager::startGarbageCollector()
{
    std::thread(runGarbageCollector).detach();
}

void EventsManager::insertClassContents(const AuditHostId &hostId, const std::tuple<time_t, uint32_t, uint64_t> &eventId, const std::string &eventType, std::string recordData)
{
    std::shared_ptr<AuditHost> hostState;
    {
        std::lock_guard<std::mutex> lock(hostsMutex);
        auto position = hostsById.find(hostId);
        if (position == hostsById.end())
        {
            auto newHost = std::make_shared<AuditHost>();
            newHost->setHostId(hostId);
            position = hostsById.emplace(hostId, std::move(newHost)).first;
        }
        hostState = position->second;
    }
    // A shared owner keeps the host alive after releasing the registry lock.
    // Per-host queue waits must not block reception or statistics for other hosts.
    hostState->insertClassContents(eventId, eventType, std::move(recordData));
}

EventsManager::HostMap EventsManager::snapshotHosts()
{
    std::lock_guard<std::mutex> lock(hostsMutex);
    // Copy owners, not event buffers; callers inspect each host under its own lock.
    return hostsById;
}

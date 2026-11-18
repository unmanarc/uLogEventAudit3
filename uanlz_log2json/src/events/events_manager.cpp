#include "events_manager.h"
#include "../globals.h"

#include <mutex>
#include <thread>

using namespace UANLZ::LOG2JSON::AuditdEvents;

Events_Manager::HostMap Events_Manager::eventsByHostName;
std::mutex Events_Manager::mutex_insert;

void garbageCollector()
{
    std::string threadName = "EVENTS_GC";
#ifndef WIN32
    pthread_setname_np(pthread_self(), threadName.c_str());
#endif
    Events_Manager::gc();
}

Events_Manager::Events_Manager()
{

}

json Events_Manager::getJSONStats()
{
    boost::json::array v;
    for (const auto &[id, hostEvents] : snapshotHosts())
    {
        boost::json::object host{{"events", boost::json::object{}}};
        host["host"] = id.hostname;
        host["ip"] = id.ip;
        host["events"].as_object()["pending"] = (uint64_t) hostEvents->getPendingEventsCount();
        host["events"].as_object()["processed"] = (uint64_t) hostEvents->getCountEventsProcessed();
        host["events"].as_object()["dropped"] = (uint64_t) hostEvents->getCountEventsDropped() ;
        v.emplace_back(std::move(host));
    }
    return v;
}

void Events_Manager::gc()
{
    for (;;)
    {
        uint64_t maxAuditWaitTime = Globals::getConfig_main()->get<uint64_t>("GC.AuditMaxAgeInSecs",10);
        uint64_t auditGCPeriod = Globals::getConfig_main()->get<uint64_t>("GC.GCPeriodInSecs",5);

        for (const auto &entry : snapshotHosts())
            entry.second->dropOldUncompletedEvents(maxAuditWaitTime);
        sleep(auditGCPeriod);
    }
}

void Events_Manager::startGC()
{
    std::thread(garbageCollector).detach();
}

void Events_Manager::insertClassContents(const auditHostID & hostID, const std::tuple<time_t, uint32_t, uint64_t> &eventId, const std::string &eventType, std::string vardata )
{
    std::shared_ptr<Audit_Host> host;
    {
        std::lock_guard<std::mutex> lock(mutex_insert);
        auto position = eventsByHostName.find(hostID);
        if (position == eventsByHostName.end())
        {
            auto created = std::make_shared<Audit_Host>();
            created->setHostID(hostID);
            position = eventsByHostName.emplace(hostID, std::move(created)).first;
        }
        host = position->second;
    }
    host->insertClassContents(eventId, eventType, std::move(vardata));
}

Events_Manager::HostMap Events_Manager::snapshotHosts()
{
    std::lock_guard<std::mutex> lock(mutex_insert);
    return eventsByHostName;
}

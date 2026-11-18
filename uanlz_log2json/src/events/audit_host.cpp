#include "audit_host.h"
#include "../globals.h"
#include "events_distributionthreads.h"
#include <vector>

using namespace std;
using namespace UANLZ::LOG2JSON::AuditdEvents;

namespace {
string eventIDString(const Audit_Event &event)
{
    const auto [seconds, milliseconds, id] = event.getEventId();
    return to_string(seconds) + "." + to_string(milliseconds) + ":" + to_string(id);
}
}

Audit_Host::Audit_Host() : countEventsDropped(0), countEventsProcessed(0)
{
    enforceMaxAge = Globals::getConfig_main()->get<bool>("GC.EnforceMaxAge", true);
}

void Audit_Host::insertClassContents(const tuple<time_t, uint32_t, uint64_t> &eventId,
                                    const string &eventType, string varData)
{
    unique_lock<mutex> lock(mEvents);
    auto position = auditEvents.find(eventId);
    if (position == auditEvents.end())
    {
        auto event = make_unique<Audit_Event>();
        event->setEventId(eventId);
        event->setHostId(hostid);
        position = auditEvents.emplace(eventId, std::move(event)).first;
    }

    if (position->second->insertClassContents(eventType, std::move(varData)) && !enforceMaxAge)
    {
        auto event = std::move(position->second);
        auditEvents.erase(position);
        lock.unlock();
        const auto id = eventIDString(*event);
        if (!Events_DistributionThreads::pushAuditEvent(std::move(event)))
        {
            LOG_APP->log0(__func__, Mantids30::Runtime::Logs::ERROR,
                          "Queue full, Event %s Dropped...", id.c_str());
            ++countEventsDropped;
        }
        else ++countEventsProcessed;
    }
}

void Audit_Host::setHostID(const auditHostID &value)
{
    lock_guard<mutex> lock(mEvents);
    if (hostid.hostname.empty()) hostid = value;
}

void Audit_Host::dropOldUncompletedEvents(const uint64_t &maxEventTimeInSeconds)
{
    vector<unique_ptr<Audit_Event>> expired;
    string hostname;
    {
        lock_guard<mutex> lock(mEvents);
        hostname = hostid.hostname;
        for (auto it = auditEvents.begin(); it != auditEvents.end();)
        {
            if (it->second->timeSinceCreation() > maxEventTimeInSeconds)
            {
                expired.push_back(std::move(it->second));
                it = auditEvents.erase(it);
            }
            else ++it;
        }
    }

    if (!enforceMaxAge && !expired.empty())
    {
        string ids;
        for (const auto &event : expired)
        {
            if (!ids.empty()) ids += ",";
            ids += eventIDString(*event);
        }
        LOG_APP->log0(__func__, Mantids30::Runtime::Logs::ERROR,
                      "Queueing uncompleted events for host='%s' (%s)...", hostname.c_str(), ids.c_str());
    }

    for (auto &event : expired)
    {
        const auto id = eventIDString(*event);
        if (!Events_DistributionThreads::pushAuditEvent(std::move(event)))
        {
            LOG_APP->log0(__func__, Mantids30::Runtime::Logs::ERROR,
                          "Queue full, Event %s Dropped...", id.c_str());
            ++countEventsDropped;
        }
        else ++countEventsProcessed;
    }
}

size_t Audit_Host::getPendingEventsCount()
{
    lock_guard<mutex> lock(mEvents);
    return auditEvents.size();
}

uint64_t Audit_Host::getCountEventsDropped() const { return countEventsDropped.load(); }
uint64_t Audit_Host::getCountEventsProcessed() const { return countEventsProcessed.load(); }

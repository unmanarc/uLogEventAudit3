/**
 * @file
 * @brief Implementation for uanlz_log2json/src/events/audit_host.
 */
#include "audit_host.h"
#include "../globals.h"
#include "events_distributionthreads.h"
#include <vector>

using namespace std;
using namespace UANLZ::LOG2JSON::AuditdEvents;

namespace {
/**
 * @brief Format an event identity for diagnostics without changing its JSON representation.
 * @param event Event supplying seconds, milliseconds and serial number.
 * @return Auditd identity in seconds.milliseconds:serial format.
 */
string formatEventId(const AuditEvent &event)
{
    const auto [seconds, milliseconds, eventLabel] = event.getEventId();
    return to_string(seconds) + "." + to_string(milliseconds) + ":" + to_string(eventLabel);
}
} // namespace

AuditHost::AuditHost()
    : droppedEventsCount(0)
    , processedEventsCount(0)
{
    enforceMaxAge = Globals::getMainConfig()->get<bool>("GC.EnforceMaxAge", true);
}

void AuditHost::insertClassContents(const tuple<time_t, uint32_t, uint64_t> &eventId, const string &eventType, string recordData)
{
    unique_lock<mutex> lock(eventsMutex);
    auto position = auditEvents.find(eventId);
    if (position == auditEvents.end())
    {
        auto event = make_unique<AuditEvent>();
        event->setEventId(eventId);
        event->setHostId(hostId);
        position = auditEvents.emplace(eventId, std::move(event)).first;
    }

    if (position->second->insertClassContents(eventType, std::move(recordData)) && !enforceMaxAge)
    {
        auto event = std::move(position->second);
        auditEvents.erase(position);
        // Detach ownership before a queue push that may wait for capacity.
        lock.unlock();
        const auto eventLabel = formatEventId(*event);
        if (!EventDistribution::pushAuditEvent(std::move(event)))
        {
            LOG_APP->log0(__func__, Mantids30::Runtime::Logs::ERROR, "Queue full, Event %s Dropped...", eventLabel.c_str());
            ++droppedEventsCount;
        }
        else
            ++processedEventsCount;
    }
}

void AuditHost::setHostId(const AuditHostId &value)
{
    lock_guard<mutex> lock(eventsMutex);
    if (hostId.hostname.empty())
        hostId = value;
}

void AuditHost::flushExpiredEvents(const uint64_t &maxEventAgeSeconds)
{
    vector<unique_ptr<AuditEvent>> expired;
    string hostname;
    {
        lock_guard<mutex> lock(eventsMutex);
        hostname = hostId.hostname;
        for (auto it = auditEvents.begin(); it != auditEvents.end();)
        {
            if (it->second->getAgeSeconds() > maxEventAgeSeconds)
            {
                expired.push_back(std::move(it->second));
                it = auditEvents.erase(it);
            }
            else
                ++it;
        }
    }

    if (!enforceMaxAge && !expired.empty())
    {
        string eventLabels;
        for (const auto &event : expired)
        {
            if (!eventLabels.empty())
                eventLabels += ",";
            eventLabels += formatEventId(*event);
        }
        LOG_APP->log0(__func__, Mantids30::Runtime::Logs::ERROR, "Queueing uncompleted events for host='%s' (%s)...", hostname.c_str(), eventLabels.c_str());
    }

    // Expired events are already detached, so queue waits hold no host lock.
    for (auto &event : expired)
    {
        const auto eventLabel = formatEventId(*event);
        if (!EventDistribution::pushAuditEvent(std::move(event)))
        {
            LOG_APP->log0(__func__, Mantids30::Runtime::Logs::ERROR, "Queue full, Event %s Dropped...", eventLabel.c_str());
            ++droppedEventsCount;
        }
        else
            ++processedEventsCount;
    }
}

size_t AuditHost::getPendingEventsCount()
{
    lock_guard<mutex> lock(eventsMutex);
    return auditEvents.size();
}

uint64_t AuditHost::getDroppedEventsCount() const
{
    return droppedEventsCount.load();
}
uint64_t AuditHost::getProcessedEventsCount() const
{
    return processedEventsCount.load();
}

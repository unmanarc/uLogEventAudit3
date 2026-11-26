/**
 * @file
 * @brief Implementation for ul3_log2json/src/events/auditd_host.
 */
#include "auditd_host.h"
#include "../globals.h"
#include "events_distributionthreads.h"
#include <vector>

using namespace std;
using namespace UL3::LOG2JSON::AuditdEvents;

namespace {
/**
 * @brief Format an event identity for diagnostics without changing its JSON representation.
 * @param event Event supplying seconds, milliseconds and serial number.
 * @return Auditd identity in seconds.milliseconds:serial format.
 */
string formatEventId(const AuditdEvent &event)
{
    const auto [seconds, milliseconds, eventLabel] = event.getEventId();
    return to_string(seconds) + "." + to_string(milliseconds) + ":" + to_string(eventLabel);
}
} // namespace

AuditdHost::AuditdHost()
    : droppedEventsCount(0)
    , processedEventsCount(0)
{
    enforceMaxAge = Globals::getMainConfig()->get<bool>("GC.EnforceMaxAge", true);
}

void AuditdHost::insertRecord(const tuple<time_t, uint32_t, uint64_t> &eventId, const string &eventType, string recordData)
{
    unique_lock<mutex> lock(eventsMutex);
    auto position = auditEvents.find(eventId);
    if (position == auditEvents.end())
    {
        auto event = make_unique<AuditdEvent>();
        event->setEventId(eventId);
        event->setHostId(hostId);
        position = auditEvents.emplace(eventId, std::move(event)).first;
    }

    if (position->second->insertRecord(eventType, std::move(recordData)) && !enforceMaxAge)
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
        {
            ++processedEventsCount;
        }
    }
}

void AuditdHost::setHostId(const AuditdHostId &value)
{
    std::scoped_lock lock(eventsMutex);
    if (hostId.hostname.empty())
    {
        hostId = value;
    }
}

void AuditdHost::flushExpiredEvents(const uint64_t &maxEventAgeSeconds)
{
    vector<unique_ptr<AuditdEvent>> expired;
    string hostname;
    {
        std::scoped_lock lock(eventsMutex);
        hostname = hostId.hostname;
        for (auto it = auditEvents.begin(); it != auditEvents.end();)
        {
            if (it->second->getAgeSeconds() > maxEventAgeSeconds)
            {
                expired.push_back(std::move(it->second));
                it = auditEvents.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    if (!enforceMaxAge && !expired.empty())
    {
        string eventLabels;
        for (const auto &event : expired)
        {
            if (!eventLabels.empty())
            {
                eventLabels += ",";
            }
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
        {
            ++processedEventsCount;
        }
    }
}

size_t AuditdHost::getPendingEventsCount()
{
    std::scoped_lock lock(eventsMutex);
    return auditEvents.size();
}

uint64_t AuditdHost::getDroppedEventsCount() const
{
    return droppedEventsCount.load();
}
uint64_t AuditdHost::getProcessedEventsCount() const
{
    return processedEventsCount.load();
}

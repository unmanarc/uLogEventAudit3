/**
 * @file
 * @brief Implementation for uanlz_log2json/src/events/audit_event.
 */
#include "audit_event.h"
#include "../vars/namedefs.h"

#include <ctime>
#include <iostream>
#include <string>

#include <boost/algorithm/string.hpp>
using namespace boost::algorithm;

using namespace std;
using namespace UANLZ::LOG2JSON::AuditdEvents;

AuditEvent::AuditEvent()
{
    creationTime = std::chrono::steady_clock::now();
    eventId = std::make_tuple<time_t, uint32_t, uint64_t>(0, 0, 0);
}

uint64_t AuditEvent::getAgeSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - creationTime).count();
}

bool AuditEvent::insertClassContents(const string &eventType, std::string recordData)
{
    auto existing = classRecords.find(eventType);
    // PATH has one record per path; other record types can span several input lines.
    if (eventType == "PATH" || existing == classRecords.end())
    {
        auto entry = classRecords.emplace(eventType, AuditClass{});
        entry->second.setRawInput(std::move(recordData));
        entry->second.setClassTypeName(eventType);
    }
    else
    {
        existing->second.getRawInput().append(" ").append(recordData);
    }
    return NameDefs::isEndEventType(eventType);
}

std::tuple<time_t, uint32_t, uint64_t> AuditEvent::getEventId() const
{
    return eventId;
}

void AuditEvent::setEventId(const std::tuple<time_t, uint32_t, uint64_t> &value)
{
    eventId = value;
}

AuditHostId AuditEvent::getHostId() const
{
    return hostId;
}

void AuditEvent::setHostId(const AuditHostId &value)
{
    hostId = value;
}

bool AuditEvent::process()
{
    for (auto &record : classRecords)
    {
        record.second.parseVariables();
    }

    return true;
}

std::set<string> AuditEvent::getClassNames()
{
    std::set<string> records;
    for (auto &record : classRecords)
        records.insert(record.first);
    return records;
}

bool AuditEvent::hasMultipleClassRecords(const string &groupName)
{
    auto result = classRecords.equal_range(groupName);
    return std::distance(result.first, result.second) > 1;
}

std::list<AuditClass *> AuditEvent::getClassRecords(const string &groupName)
{
    list<AuditClass *> records;
    for (auto &record : classRecords)
    {
        if (record.first == groupName)
            records.push_back(&record.second);
    }
    return records;
}

AuditClass *AuditEvent::getFirstClassRecord(const string &groupName)
{
    if (auto it = classRecords.find(groupName); it != classRecords.end())
        return &it->second;
    return nullptr;
}

boost::json::object AuditEvent::getJson()
{
    time_t unixTime = std::get<0>(eventId);
    std::tm localTime{};
    char formattedTime[32]{};
    if (localtime_r(&unixTime, &localTime))
        std::strftime(formattedTime, sizeof(formattedTime), "%a %b %e %H:%M:%S %Y", &localTime);
    std::string formattedTimestamp = formattedTime;
    while (ends_with(formattedTimestamp, "\n"))
        formattedTimestamp.pop_back();

    std::string auditId = to_string(unixTime) + "." + to_string(std::get<1>(eventId)) + ":" + to_string(std::get<2>(eventId));

    boost::json::object info{{"source", "auditd"},
                             {"parserVersion", 1},
                             {"unixTime", (int64_t) unixTime},
                             {"msecs", std::get<1>(eventId)},
                             {"id", (uint64_t) std::get<2>(eventId)},
                             {"host", hostId.hostname},
                             {"ip", hostId.ipAddress},
                             {"time", formattedTimestamp}};
    boost::json::object records;

    std::set<string> classes = getClassNames();
    for (const string &className : classes)
    {
        boost::json::array entries;
        auto [first, last] = classRecords.equal_range(className);
        for (auto it = first; it != last; ++it)
        {
            if (className != "INFO")
            {
                entries.emplace_back(it->second.getJson());
            }
            // TODO: limit max classes
        }
        if (!entries.empty() && Mantids30::DataFormat::JSON::HAS_MEMBER(entries.front(), "key"))
        {
            info.erase("key");
            info.emplace("key", Mantids30::DataFormat::JSON::MEMBER_OR_NULL(entries.front(), "key"));
        }
        if (!entries.empty())
            records.emplace(className, std::move(entries));
    }
    boost::json::object eventJson{{"AUDITD", boost::json::object{{"id", auditId}}}, {"INFO", std::move(info)}};
    for (auto &record : records)
    {
        eventJson.erase(record.key());
        eventJson.emplace(record.key(), std::move(record.value()));
    }
    return eventJson;
}

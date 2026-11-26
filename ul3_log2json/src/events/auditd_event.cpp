/**
 * @file
 * @brief Implementation for ul3_log2json/src/events/auditd_event.
 */
#include "auditd_event.h"
#include "../vars/namedefs.h"

#include <ctime>
#include <iostream>
#include <string>

#include <boost/algorithm/string.hpp>
using namespace boost::algorithm;

using namespace std;
using namespace UL3::LOG2JSON::AuditdEvents;

AuditdEvent::AuditdEvent()
{
    creationTime = std::chrono::steady_clock::now();
    eventId = std::make_tuple<time_t, uint32_t, uint64_t>(0, 0, 0);
}

uint64_t AuditdEvent::getAgeSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - creationTime).count();
}

bool AuditdEvent::insertRecord(const string &eventType, std::string recordData)
{
    auto existing = recordsByType.find(eventType);
    // PATH has one record per path; other record types can span several input lines.
    if (eventType == "PATH" || existing == recordsByType.end())
    {
        auto entry = recordsByType.emplace(eventType, AuditdRecord{});
        entry->second.setRawInput(std::move(recordData));
        entry->second.setRecordType(eventType);
    }
    else
    {
        existing->second.getRawInput().append(" ").append(recordData);
    }
    return NameDefs::isEndEventType(eventType);
}

std::tuple<time_t, uint32_t, uint64_t> AuditdEvent::getEventId() const
{
    return eventId;
}

void AuditdEvent::setEventId(const std::tuple<time_t, uint32_t, uint64_t> &value)
{
    eventId = value;
}

AuditdHostId AuditdEvent::getHostId() const
{
    return hostId;
}

void AuditdEvent::setHostId(const AuditdHostId &value)
{
    hostId = value;
}

bool AuditdEvent::process()
{
    for (auto &record : recordsByType)
    {
        record.second.parseVariables();
    }

    return true;
}

std::set<string> AuditdEvent::getRecordTypes()
{
    std::set<string> records;
    for (auto &record : recordsByType)
    {
        records.insert(record.first);
    }
    return records;
}

bool AuditdEvent::hasMultipleRecords(const string &recordType)
{
    auto result = recordsByType.equal_range(recordType);
    return std::distance(result.first, result.second) > 1;
}

std::list<AuditdRecord *> AuditdEvent::getRecords(const string &recordType)
{
    list<AuditdRecord *> records;
    for (auto &record : recordsByType)
    {
        if (record.first == recordType)
        {
            records.push_back(&record.second);
        }
    }
    return records;
}

AuditdRecord *AuditdEvent::getFirstRecord(const string &recordType)
{
    if (auto it = recordsByType.find(recordType); it != recordsByType.end())
    {
        return &it->second;
    }
    return nullptr;
}

boost::json::object AuditdEvent::getJson()
{
    time_t unixTime = std::get<0>(eventId);
    std::tm localTime{};
    char formattedTime[32]{};
    if (localtime_r(&unixTime, &localTime))
    {
        std::strftime(formattedTime, sizeof(formattedTime), "%a %b %e %H:%M:%S %Y", &localTime);
    }
    std::string formattedTimestamp = formattedTime;
    while (ends_with(formattedTimestamp, "\n"))
    {
        formattedTimestamp.pop_back();
    }

    std::string auditId = to_string(unixTime) + "." + to_string(std::get<1>(eventId)) + ":" + to_string(std::get<2>(eventId));

    boost::json::object info{{"source", "auditd"},
                             {"parserVersion", 1},
                             {"unixTime", static_cast<int64_t>(unixTime)},
                             {"msecs", std::get<1>(eventId)},
                             {"id", std::get<2>(eventId)},
                             {"host", hostId.hostname},
                             {"ip", hostId.ipAddress},
                             {"time", formattedTimestamp}};
    boost::json::object records;

    std::set<string> recordTypes = getRecordTypes();
    for (const string &recordType : recordTypes)
    {
        boost::json::array entries;
        auto [first, last] = recordsByType.equal_range(recordType);
        for (auto it = first; it != last; ++it)
        {
            if (recordType != "INFO")
            {
                entries.emplace_back(it->second.getJson());
            }
            // TODO: limit the number of record types.
        }
        if (!entries.empty() && Mantids30::DataFormat::JSON::HAS_MEMBER(entries.front(), "key"))
        {
            info.erase("key");
            info.emplace("key", Mantids30::DataFormat::JSON::MEMBER_OR_NULL(entries.front(), "key"));
        }
        if (!entries.empty())
        {
            records.emplace(recordType, std::move(entries));
        }
    }
    boost::json::object eventJson{{"AUDITD", boost::json::object{{"id", auditId}}}, {"INFO", std::move(info)}};
    for (auto &record : records)
    {
        eventJson.erase(record.key());
        eventJson.emplace(record.key(), std::move(record.value()));
    }
    return eventJson;
}

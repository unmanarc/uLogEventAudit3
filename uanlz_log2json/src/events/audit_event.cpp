#include "audit_event.h"
#include "../vars/namedefs.h"

#include <ctime>
#include <iostream>
#include <string>

#include <boost/algorithm/string.hpp>
using namespace boost::algorithm;

using namespace std;
using namespace UANLZ::LOG2JSON::AuditdEvents;


Audit_Event::Audit_Event()
{
    creationTime = std::chrono::steady_clock::now();
    eventId = std::make_tuple<time_t, uint32_t, uint64_t>(0,0,0);
}

uint64_t Audit_Event::timeSinceCreation()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - creationTime).count();
}

bool Audit_Event::insertClassContents(const string &eventType, std::string varData)
{
    auto existing = classVars.find(eventType);
    if (eventType == "PATH" || existing == classVars.end())
    {
        auto entry = classVars.emplace(eventType, Audit_ClassType{});
        entry->second.setRawInput(std::move(varData));
        entry->second.setClassTypeName(eventType);
    }
    else
    {
        existing->second.getRawInput().append(" ").append(varData);
    }
    return NameDefs::isEndEventType(eventType);
}

std::tuple<time_t, uint32_t, uint64_t> Audit_Event::getEventId() const
{
    return eventId;
}

void Audit_Event::setEventId(const std::tuple<time_t, uint32_t, uint64_t> &value)
{
    eventId = value;
}

auditHostID Audit_Event::getHostId() const
{
    return hostId;
}

void Audit_Event::setHostId(const auditHostID &value)
{
    hostId = value;
}

bool Audit_Event::process()
{
    for (auto & i : classVars)
    {
        i.second.processToVarContent();
    }

    // TODO: classVars -> sql string?
    return true;
}

std::set<string> Audit_Event::getClassesNames()
{
    std::set<string> r;
    for (auto & i : classVars)
        r.insert(i.first);
    return r;

}

bool Audit_Event::isMultiContainer(const string &groupName)
{
    auto result = classVars.equal_range(groupName);
    return std::distance(result.first, result.second) > 1;
}

std::list<Audit_ClassType *> Audit_Event::getMultiLineClassVars(const string &groupName)
{
    list<Audit_ClassType *> r;
    for (auto & i : classVars)
    {
        if (i.first == groupName) r.push_back(&i.second);
    }
    return r;
}


Audit_ClassType *Audit_Event::getClassVars(const string &groupName)
{
    if (auto it = classVars.find(groupName); it != classVars.end())
        return &it->second;
    return nullptr;
}

json Audit_Event::getJSON()
{
    boost::json::object x{{"AUDITD", boost::json::object{}}, {"INFO", boost::json::object{}}};

    time_t unixTime = std::get<0>(eventId);
    std::tm localTime{};
    char formattedTime[32]{};
    if (localtime_r(&unixTime, &localTime))
        std::strftime(formattedTime, sizeof(formattedTime), "%a %b %e %H:%M:%S %Y", &localTime);
    std::string sTime = formattedTime;
    while (ends_with(sTime,"\n")) sTime.pop_back();

    std::string auditdID =  to_string(unixTime) + "." + to_string(std::get<1>(eventId)) + ":" + to_string(std::get<2>(eventId));

    x["AUDITD"].as_object()["id"]  = auditdID;

    x["INFO"].as_object()["source"] = "auditd";
    x["INFO"].as_object()["parserVersion"]  = 1;

    x["INFO"].as_object()["unixTime"]  = (int64_t)unixTime;
    x["INFO"].as_object()["msecs"]     = std::get<1>(eventId);
    x["INFO"].as_object()["id"]        = (uint64_t)std::get<2>(eventId);

    x["INFO"].as_object()["host"]      = hostId.hostname;
    x["INFO"].as_object()["ip"]        = hostId.ip;
    x["INFO"].as_object()["time"]      = sTime;

    std::set<string> classes = getClassesNames();
    for (const string & className : classes)
    {
        boost::json::array entries;
        auto [first, last] = classVars.equal_range(className);
        for (auto it = first; it != last; ++it)
        {
            if (className != "INFO")
            {
                entries.emplace_back(it->second.getJSON());
            }
            // TODO: limit max classes
        }
        if (!entries.empty() && Mantids30::DataFormat::JSON::HAS_MEMBER(entries.front(), "key"))
        {
            x["INFO"].as_object()["key"] = entries.front().as_object().at("key");
        }
        if (!entries.empty()) x[className] = std::move(entries);
    }
    return x;
}


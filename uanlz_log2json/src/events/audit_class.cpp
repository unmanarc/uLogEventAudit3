#include "audit_class.h"

#include "../globals.h"

#include <sys/un.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <boost/regex.hpp>

#include <boost/algorithm/string.hpp>
using namespace boost::algorithm;

static boost::regex exSyslogAuditdData1("(?<AUDITD_DATA_VARNAME>[^=^ ]+)=(?<AUDITD_DATA_VARVALUE>\"[^\"]+\")");
static boost::regex exSyslogAuditdData2("(?<AUDITD_DATA_VARNAME>[^=^ ]+)=(?<AUDITD_DATA_VARVALUE>[^ ^\"]+)");

using namespace std;
using namespace UANLZ::LOG2JSON::AuditdEvents;

string &Audit_ClassType::getRawInput()
{
    return rawInput;
}

void Audit_ClassType::setRawInput(string value)
{
    rawInput = std::move(value);
}

unsigned char fromHex16(const char *value);

bool Audit_ClassType::processToVarContent()
{
    contentVars.clear();
    boost::match_results<string::const_iterator> whatDataDecomposed;
    boost::match_flag_type flags = boost::match_default;

    for (string::const_iterator start = rawInput.begin(), end =  rawInput.end();
         boost::regex_search(start, end, whatDataDecomposed, exSyslogAuditdData1, flags);
         start = whatDataDecomposed[0].second)
    {
        parseVar(string(whatDataDecomposed[1].first, whatDataDecomposed[1].second),string(whatDataDecomposed[2].first, whatDataDecomposed[2].second));
    }

    for (string::const_iterator start = rawInput.begin(), end =  rawInput.end();
         boost::regex_search(start, end, whatDataDecomposed, exSyslogAuditdData2, flags);
         start = whatDataDecomposed[0].second)
    {
        parseVar(string(whatDataDecomposed[1].first, whatDataDecomposed[1].second),string(whatDataDecomposed[2].first, whatDataDecomposed[2].second));
    }

    return mergeSpplitedVars();
}

string Audit_ClassType::getClassTypeName() const
{
    return classTypeName;
}

void Audit_ClassType::setClassTypeName(const string &value)
{
    classTypeName = value;
}

json Audit_ClassType::getJSON()
{
    boost::json::object v{};

    bool normal = true;
    if (classTypeName == "EXECVE")
    {
        if (contentVars.find("argc")!=contentVars.end())
        {
            normal = false;
            v["argc"] = contentVars["argc"].getFancy();
            v["argv"] = boost::json::array{};
            std::string cmdline;
            for (uint32_t i=0; i<contentVars["argc"].asUInt32() && i<65536; i++)
            {
                std::string curVar = "a" + to_string(i);
                if (contentVars.find(curVar)!=contentVars.end())
                {
                    v["argv"].as_array().emplace_back(contentVars[curVar].getFancy());
                    cmdline += contentVars[curVar].getFancy() + " ";
                }
                else
                {
                    v["argv"].as_array().emplace_back("__UAA_PARSER_NOT_FOUND__");
                    cmdline += "__UAA_PARSER_NOT_FOUND__ ";
                }
            }
            if (!cmdline.empty()) cmdline.pop_back();
            v["cmdline"] = cmdline;
        }
    }

    if (normal)
    {
        for ( auto & i : contentVars)
        {
            v[i.first] = i.second.getFancy();
        }
    }

    return v;
}

void Audit_ClassType::parseVar(const string &varName, const string &varValue)
{

    if (!contentVars[varName].parse( classTypeName, varName,  varValue ))
    {
        LOG_APP->log0(__func__,Mantids30::Runtime::Logs::ERROR, "Unable to parse variable '%s' with value '%s'", varName.c_str(), varValue.c_str());
    }

    if (classTypeName == "SOCKADDR" && varName == "saddr" &&
        varValue.size() >= 2 * sizeof(sa_family_t) && varValue.size() < 250 &&
        varValue.size() % 2 == 0 &&
        varValue.find_first_not_of("0123456789abcdefABCDEF") == string::npos)
    {
        string family, laddr, path;
        unsigned short port = 0;

        std::array<char, 128> data{};
        const auto dataSize = varValue.size() / 2;
        for (size_t i = 0; i < dataSize; ++i)
            data[i] = fromHex16(varValue.data() + 2 * i);

        sa_family_t socketFamily{};
        std::memcpy(&socketFamily, data.data(), sizeof(socketFamily));
        switch (socketFamily)
        {
        case AF_INET:
        {
            if (dataSize < sizeof(sockaddr_in)) return;
            sockaddr_in address{};
            std::memcpy(&address, data.data(), sizeof(address));
            char ipAddr[INET_ADDRSTRLEN]{};
            if (!inet_ntop(AF_INET, &address.sin_addr, ipAddr, sizeof(ipAddr))) return;
            family = "AF_INET";
            port = ntohs(address.sin_port);
            laddr = ipAddr;
        } break;
        case AF_INET6:
        {
            if (dataSize < sizeof(sockaddr_in6)) return;
            sockaddr_in6 address{};
            std::memcpy(&address, data.data(), sizeof(address));
            char ipAddr[INET6_ADDRSTRLEN]{};
            if (!inet_ntop(AF_INET6, &address.sin6_addr, ipAddr, sizeof(ipAddr))) return;
            family = "AF_INET6";
            port = ntohs(address.sin6_port);
            laddr = ipAddr;
        } break;
        case AF_UNIX:
        {
            constexpr auto offset = offsetof(sockaddr_un, sun_path);
            if (dataSize < offset) return;
            sockaddr_un address{};
            std::memcpy(&address, data.data(), std::min(dataSize, sizeof(address)));
            const auto length = std::min(dataSize - offset, sizeof(address.sun_path));
            path.assign(address.sun_path, strnlen(address.sun_path, length));
            family = "AF_UNIX";
            laddr = "127.0.0.1";
        } break;
        default:
            break;
        }


        contentVars["family"].parse( classTypeName, "family",  family );
        contentVars["ipaddr"].parse( classTypeName, "ipaddr",  laddr );
        if (path.size()) contentVars["unix_path"].parse( classTypeName, "unix_path",  path );
        contentVars["port"].parse( classTypeName, "port",  to_string(port) );
    }
}

bool Audit_ClassType::mergeSpplitedVars()
{
    set<string> spplitedVars = getSpplitedVarNames();
    for (const auto & varName : spplitedVars)
    {
        // Take the next vars, append to the first one and remove the extra...
        string varNameWithZeroPos = getVarNameByPos(varName,0);
        contentVars[varName].append(contentVars[varNameWithZeroPos]);
        contentVars.erase(varNameWithZeroPos);

        size_t i=1;
        for (std::string varNameWithPos = getVarNameByPos(varName,i++); contentVars.find(varNameWithPos) != contentVars.end();  varNameWithPos = getVarNameByPos(varName,i++))
        {
            // Append each variable, and then destroy it.
            contentVars[varName].append(contentVars[varNameWithPos]);
            contentVars.erase(varNameWithPos);
        }
    }


    // Find any variable ending with ]
    for (const auto & i : contentVars)
    {
        if (ends_with(i.first,"]"))
        {
            LOG_APP->log0(__func__,Mantids30::Runtime::Logs::ERROR, "Orphan variable '%s' can't be reconstructed", i.first.c_str());
        }
    }

    return true;
}

set<string> Audit_ClassType::getSpplitedVarNames()
{
    set<string> spplitedVars;
    for ( const auto & i :  contentVars)
    {
        if (ends_with(i.first,"[0]"))
        {
            string varName = i.first;
            varName.pop_back();varName.pop_back();varName.pop_back();
            spplitedVars.insert(varName);
        }
    }
    return spplitedVars;
}

string Audit_ClassType::getVarNameByPos(const string &varName, const size_t & pos)
{
    return varName + "[" + to_string(pos) + "]";
}

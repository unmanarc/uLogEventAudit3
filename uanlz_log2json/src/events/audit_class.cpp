/**
 * @file
 * @brief Implementation for uanlz_log2json/src/events/audit_class.
 */
#include "audit_class.h"

#include "../globals.h"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>

#include <boost/regex.hpp>

#include <boost/algorithm/string.hpp>
using namespace boost::algorithm;

// Consume quoted fields as a whole so embedded key=value text stays literal.
static const boost::regex variablePattern(R"audit((?:^|[ \t])([^= \t]+)=("[^"]*"|[^" \t]+)(?=$|[ \t]))audit");

using namespace std;
using namespace UANLZ::LOG2JSON::AuditdEvents;

string &AuditClass::getRawInput()
{
    return rawInput;
}

void AuditClass::setRawInput(string value)
{
    rawInput = std::move(value);
}

/**
 * @brief Decode two consecutive hexadecimal characters into one byte.
 * @param encodedValue Borrowed buffer containing at least two readable characters.
 * @pre The caller has validated the field length and hexadecimal encoding.
 * @return Decoded byte value.
 */
unsigned char decodeHexByte(const char *encodedValue);

bool AuditClass::parseVariables()
{
    variablesByName.clear();
    for (boost::sregex_iterator field(rawInput.begin(), rawInput.end(), variablePattern), end; field != end; ++field)
    {
        parseVariable((*field)[1].str(), (*field)[2].str());
    }

    return mergeSplitVariables();
}

string AuditClass::getClassTypeName() const
{
    return classTypeName;
}

void AuditClass::setClassTypeName(const string &value)
{
    classTypeName = value;
}

boost::json::object AuditClass::getJson()
{
    if (classTypeName == "EXECVE")
    {
        if (auto argc = variablesByName.find("argc"); argc != variablesByName.end())
        {
            boost::json::array arguments;
            std::string commandLine;
            for (uint32_t index = 0; index < argc->second.asUint32() && index < 65536; index++)
            {
                const auto argument = variablesByName.find("a" + to_string(index));
                const std::string displayValue = argument != variablesByName.end() ? argument->second.getDisplayValue() : "__UAA_PARSER_NOT_FOUND__";
                arguments.emplace_back(displayValue);
                commandLine += displayValue + " ";
            }
            if (!commandLine.empty())
                commandLine.pop_back();
            return boost::json::object{{"argc", argc->second.getDisplayValue()}, {"argv", std::move(arguments)}, {"cmdline", commandLine}};
        }
    }

    boost::json::object recordJson;
    for (const auto &variable : variablesByName)
    {
        recordJson.emplace(variable.first, variable.second.getDisplayValue());
    }
    return recordJson;
}

void AuditClass::parseVariable(const string &variableName, const string &variableValue)
{
    if (!variablesByName[variableName].parse(classTypeName, variableName, variableValue))
    {
        LOG_APP->log0(__func__, Mantids30::Runtime::Logs::ERROR, "Unable to parse variable '%s' with value '%s'", variableName.c_str(), variableValue.c_str());
    }

    if (classTypeName == "SOCKADDR" && variableName == "saddr" && variableValue.size() >= 2 * sizeof(sa_family_t) && variableValue.size() < 250 && variableValue.size() % 2 == 0
        && boost::all(variableValue, boost::is_any_of("0123456789abcdefABCDEF")))
    {
        string family, ipAddress, path;
        unsigned short port = 0;

        std::array<char, 128> data{};
        const auto addressSize = variableValue.size() / 2;
        for (size_t index = 0; index < addressSize; ++index)
            data[index] = decodeHexByte(variableValue.data() + 2 * index);

        // Socket records are byte sequences; copy into aligned types before reading fields.
        sa_family_t socketFamily{};
        std::memcpy(&socketFamily, data.data(), sizeof(socketFamily));
        switch (socketFamily)
        {
        case AF_INET:
        {
            if (addressSize < sizeof(sockaddr_in))
                return;
            sockaddr_in address{};
            std::memcpy(&address, data.data(), sizeof(address));
            char addressText[INET_ADDRSTRLEN]{};
            if (!inet_ntop(AF_INET, &address.sin_addr, addressText, sizeof(addressText)))
                return;
            family = "AF_INET";
            port = ntohs(address.sin_port);
            ipAddress = addressText;
        }
        break;
        case AF_INET6:
        {
            if (addressSize < sizeof(sockaddr_in6))
                return;
            sockaddr_in6 address{};
            std::memcpy(&address, data.data(), sizeof(address));
            char addressText[INET6_ADDRSTRLEN]{};
            if (!inet_ntop(AF_INET6, &address.sin6_addr, addressText, sizeof(addressText)))
                return;
            family = "AF_INET6";
            port = ntohs(address.sin6_port);
            ipAddress = addressText;
        }
        break;
        case AF_UNIX:
        {
            constexpr auto offset = offsetof(sockaddr_un, sun_path);
            if (addressSize < offset)
                return;
            sockaddr_un address{};
            std::memcpy(&address, data.data(), std::min(addressSize, sizeof(address)));
            const auto length = std::min(addressSize - offset, sizeof(address.sun_path));
            path.assign(address.sun_path, strnlen(address.sun_path, length));
            family = "AF_UNIX";
            ipAddress = "127.0.0.1";
        }
        break;
        default:
            break;
        }

        variablesByName["family"].parse(classTypeName, "family", family);
        variablesByName["ipaddr"].parse(classTypeName, "ipaddr", ipAddress);
        if (path.size())
            variablesByName["unix_path"].parse(classTypeName, "unix_path", path);
        variablesByName["port"].parse(classTypeName, "port", to_string(port));
    }
}

bool AuditClass::mergeSplitVariables()
{
    // Merge only contiguous chunks: missing indexes leave later chunks visible as orphans.
    set<string> splitVariableNames = getSplitVariableNames();
    for (const auto &variableName : splitVariableNames)
    {
        string firstChunkName = getChunkVariableName(variableName, 0);
        variablesByName[variableName].append(variablesByName[firstChunkName]);
        variablesByName.erase(firstChunkName);

        size_t chunkIndex = 1;
        for (std::string chunkName = getChunkVariableName(variableName, chunkIndex++); variablesByName.find(chunkName) != variablesByName.end();
             chunkName = getChunkVariableName(variableName, chunkIndex++))
        {
            variablesByName[variableName].append(variablesByName[chunkName]);
            variablesByName.erase(chunkName);
        }
    }

    for (const auto &variable : variablesByName)
    {
        if (ends_with(variable.first, "]"))
        {
            LOG_APP->log0(__func__, Mantids30::Runtime::Logs::ERROR, "Orphan variable '%s' can't be reconstructed", variable.first.c_str());
        }
    }

    return true;
}

set<string> AuditClass::getSplitVariableNames()
{
    set<string> splitVariableNames;
    for (const auto &variable : variablesByName)
    {
        if (ends_with(variable.first, "[0]"))
        {
            string variableName = variable.first;
            variableName.pop_back();
            variableName.pop_back();
            variableName.pop_back();
            splitVariableNames.insert(variableName);
        }
    }
    return splitVariableNames;
}

string AuditClass::getChunkVariableName(const string &variableName, const size_t &position)
{
    return variableName + "[" + to_string(position) + "]";
}

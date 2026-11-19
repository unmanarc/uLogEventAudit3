/**
 * @file
 * @brief Implementation for uanlz_log2json/src/events/audit_var.
 */
#include "audit_var.h"
#include "../vars/namedefs.h"
#include <Mantids30/Core/Helpers/encoders.h>
#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>

using namespace boost::algorithm;
using namespace std;
using namespace UANLZ::LOG2JSON::AuditdEvents;

bool AuditVariable::append(const AuditVariable &other)
{
    if (!other.isValid())
    {
        return setValid(false);
    }
    rawValue += other.rawValue;
    displayValue += other.displayValue;
    return setValid(true);
}

/**
 * @brief Check that a field contains only auditd uppercase hex digits.
 * @param encodedValue Input text to inspect.
 * @return True for an empty string or a string containing only 0-9 and A-F.
 */
inline bool isUppercaseHexString(const std::string &encodedValue)
{
    return std::all_of(encodedValue.begin(), encodedValue.end(), [](unsigned char digit) { return (digit >= '0' && digit <= '9') || (digit >= 'A' && digit <= 'F'); });
}

/**
 * @brief Identify EXECVE argument fields, including indexed fragments.
 * @param eventType Auditd record type containing the field.
 * @param variableName Field name to classify.
 * @return True for an EXECVE argument name rather than its length metadata.
 */
inline bool isExecveArgument(const string &eventType, const string &variableName)
{
    static const boost::regex argumentPattern(R"(a[0-9]+(\[[0-9]+\])?)");
    return eventType == "EXECVE" && boost::regex_match(variableName, argumentPattern);
}

void AuditVariable::updateDisplayValue()
{
    displayValue = rawValue;
    for (char &byte : displayValue)
    {
        if (byte == '\0' || byte == '!')
        {
            byte = ' ';
        }
    }
}

size_t AuditVariable::getRawValueSize() const
{
    return rawValue.size();
}

uint32_t AuditVariable::asUint32() const
{
    return strtoul(displayValue.c_str(), nullptr, 10);
}

const string &AuditVariable::getDisplayValue() const
{
    return displayValue;
}

const char *AuditVariable::getRawValue() const
{
    return rawValue.c_str();
}

bool AuditVariable::isValid() const
{
    return valid;
}

bool AuditVariable::setValid(bool isValid)
{
    valid = isValid;
    return valid;
}

bool AuditVariable::parse(const string &eventType, const string &variableName, const string &variableValue)
{
    rawValue.clear();
    displayValue.clear();
    if (variableValue.empty())
    {
        return setValid(false);
    }

    if (variableValue.size() >= 2
        && ((starts_with(variableValue, "'") && ends_with(variableValue, "'")) || (starts_with(variableValue, "\"") && ends_with(variableValue, "\""))
            || (starts_with(variableValue, "(") && ends_with(variableValue, ")")) || (starts_with(variableValue, "{") && ends_with(variableValue, "}"))))
    {
        rawValue = variableValue.substr(1, variableValue.size() - 2);

        updateDisplayValue();

        return setValid(true);
    }
    else if (NameDefs::isTextVar(variableName) || isExecveArgument(eventType, variableName))
    {
        // Unquoted auditd text is hexadecimal; numeric and other literal fields use the final branch.
        if (isUppercaseHexString(variableValue) && variableValue.size() % 2 == 0)
        {
            rawValue.resize(variableValue.size() / 2);
            Mantids30::Core::Helpers::Encoders::fromHex(variableValue, reinterpret_cast<unsigned char *>(rawValue.data()), rawValue.size());

            updateDisplayValue();
            return setValid(true);
        }
        else
        {
            return setValid(false);
        }
    }
    else // Take the value as is.
    {
        rawValue = variableValue;
        updateDisplayValue();
        return setValid(true);
    }

    return setValid(false);
}

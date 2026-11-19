/**
 * @file
 * @brief Implementation for uanlz_log2json/src/events/audit_var.
 */
#include "audit_var.h"
#include "../vars/namedefs.h"
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>

using namespace boost::algorithm;
using namespace std;
using namespace UANLZ::LOG2JSON::AuditdEvents;

bool AuditVariable::append(const AuditVariable &other)
{
    if (!other.isValid())
        return setValid(false);
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
    return boost::all(encodedValue, boost::is_any_of("0123456789ABCDEF"));
}

/**
 * @brief Convert one ASCII hexadecimal digit to its numeric nibble.
 * @param encodedValue Hexadecimal character to decode.
 * @return Nibble value, or zero for an unrecognized character.
 */
inline unsigned char decodeHexDigit(const char encodedValue)
{
    if (encodedValue >= 'A' && encodedValue <= 'F')
        return (encodedValue - 'A') + 10;
    else if (encodedValue >= '0' && encodedValue <= '9')
        return (encodedValue - '0');
    else if (encodedValue >= 'a' && encodedValue <= 'f')
        return (encodedValue - 'a') + 10;
    return 0;
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

/**
 * @brief Decode two consecutive hexadecimal characters into one byte.
 * @param encodedValue Borrowed buffer containing at least two readable characters.
 * @pre The caller has validated the field length and hexadecimal encoding.
 * @return Decoded byte value.
 */
unsigned char decodeHexByte(const char *encodedValue)
{
    return decodeHexDigit(encodedValue[0]) * 0x10 + decodeHexDigit(encodedValue[1]);
}

void AuditVariable::updateDisplayValue()
{
    displayValue = rawValue;
    for (char &byte : displayValue)
    {
        if (byte == '\0' || byte == '!')
            byte = ' ';
    }
}

size_t AuditVariable::getRawValueSize() const
{
    return rawValue.size();
}

uint32_t AuditVariable::asUint32() const
{
    return strtoul(displayValue.c_str(), 0, 10);
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
        return setValid(false);

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
            for (size_t byteIndex = 0; byteIndex < rawValue.size(); ++byteIndex)
                rawValue[byteIndex] = decodeHexByte(variableValue.data() + 2 * byteIndex);

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

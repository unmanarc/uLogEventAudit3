/**
 * @file
 * @brief Declarations and contracts for common/audit_json.
 */
#pragma once
#include <Mantids30/DataFormat/JSON/json.h>
#include <istream>
#include <iterator>
#include <string>
#include <utility>

namespace UANLZ::JSON {

/**
 * @brief Read a JSON configuration array, normalizing legacy null to an empty array.
 * @param input Stream containing the complete JSON configuration document.
 * @param result Output JSON value, assigned only after successful parsing.
 * @return False for I/O errors, malformed JSON or a non-array/non-null root.
 */
inline bool readConfigArray(std::istream &input, boost::json::value &result)
{
    const std::string document{std::istreambuf_iterator<char>(input), {}};
    boost::system::error_code error;
    auto parsed = boost::json::parse(document, error);
    if (input.bad() || error || (!parsed.is_array() && !parsed.is_null()))
        return false;
    result = parsed.is_null() ? boost::json::value(boost::json::array{}) : std::move(parsed);
    return true;
}

/**
 * @brief Append the legacy indented JSON representation recursively to a string.
 * @param output Destination string to append formatted JSON to.
 * @param value Value to serialize.
 * @param depth Current object/array nesting level used for indentation.
 */
inline void appendStyled(std::string &output, const boost::json::value &value, unsigned depth)
{
    const auto indent = [&](unsigned level) { output.append(level * 3, ' '); };
    const auto *object = value.if_object();
    if (object && !object->empty())
    {
        output += "{\n";
        bool first = true;
        for (const auto &member : *object)
        {
            if (!first)
                output += ",\n";
            first = false;
            indent(depth + 1);
            output += boost::json::serialize(boost::json::value(member.key()));
            output += " : ";
            appendStyled(output, member.value(), depth + 1);
        }
        output += '\n';
        indent(depth);
        output += '}';
    }
    else if (value.is_array() && !value.as_array().empty())
    {
        output += "[\n";
        bool first = true;
        for (const auto &element : value.as_array())
        {
            if (!first)
                output += ",\n";
            first = false;
            indent(depth + 1);
            appendStyled(output, element, depth + 1);
        }
        output += '\n';
        indent(depth);
        output += ']';
    }
    else
        output += Mantids30::DataFormat::JSON::toString(value);
}

/**
 * @brief Return indented JSON followed by a newline for action arguments and TCP output.
 * @param value Value to serialize.
 */
inline std::string toStyledString(const boost::json::value &value)
{
    std::string output;
    appendStyled(output, value, 0);
    output += '\n';
    return output;
}
} // namespace UANLZ::JSON

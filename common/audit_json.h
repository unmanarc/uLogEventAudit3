#pragma once
#include <Mantids30/DataFormat/JSON/json.h>
#include <istream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

using json = boost::json::value;

namespace UANLZ::JSON {
// Keep rule positions and input numbers from wrapping or accepting fractions.
inline uint32_t asUInt32(const json &value, std::string_view key, uint32_t fallback)
{
    const auto number = Mantids30::DataFormat::JSON::ASUINT64(
        value, key, std::numeric_limits<uint64_t>::max());
    return number <= std::numeric_limits<uint32_t>::max() ? static_cast<uint32_t>(number) : fallback;
}

// Configuration documents are arrays. Accept legacy null files as empty lists.
inline bool readConfigArray(std::istream &input, json &result)
{
    const std::string document{std::istreambuf_iterator<char>(input), {}};
    boost::system::error_code error;
    auto parsed = boost::json::parse(document, error);
    if (input.bad() || error || (!parsed.is_array() && !parsed.is_null()))
        return false;
    result = parsed.is_null() ? json(boost::json::array{}) : std::move(parsed);
    return true;
}

// Preserve the formatted action argument and TCP output modes with Boost.JSON.
inline void appendStyled(std::string &output, const json &value, unsigned depth)
{
    const auto indent = [&](unsigned level) { output.append(level * 3, ' '); };
    if (value.is_object() && !value.as_object().empty())
    {
        output += "{\n";
        bool first = true;
        for (const auto &member : value.as_object())
        {
            if (!first) output += ",\n";
            first = false;
            indent(depth + 1);
            output += boost::json::serialize(json(member.key()));
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
            if (!first) output += ",\n";
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

inline std::string toStyledString(const json &value)
{
    std::string output;
    appendStyled(output, value, 0);
    output += '\n';
    return output;
}
}

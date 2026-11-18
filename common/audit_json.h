#pragma once
#include <json/json.h>
#include <boost/json.hpp>
#include <sstream>
using json = Json::Value;
namespace UANLZ::JSON {
inline boost::json::value toMantids(const json &value) {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return boost::json::parse(Json::writeString(writer, value));
}
inline json fromMantids(const boost::json::value &value) {
    std::istringstream input(boost::json::serialize(value));
    Json::CharReaderBuilder reader;
    json result;
    std::string error;
    if (!Json::parseFromStream(reader, input, &result, &error))
        throw std::runtime_error(error);
    return result;
}
}
#define JSON_ASSTRING(j,k,d) ((j).isObject() && (j).isMember(k) && (j)[k].isString() ? (j)[k].asString() : (d))
#define JSON_ASCSTRING(j,k,d) ((j).isObject() && (j).isMember(k) && (j)[k].isString() ? (j)[k].asCString() : (d))
#define JSON_ASBOOL(j,k,d) ((j).isObject() && (j).isMember(k) && (j)[k].isBool() ? (j)[k].asBool() : (d))
#define JSON_ASUINT(j,k,d) ((j).isObject() && (j).isMember(k) && (j)[k].isUInt() ? (j)[k].asUInt() : (d))
#define JSON_ASINT(j,k,d) ((j).isObject() && (j).isMember(k) && (j)[k].isInt() ? (j)[k].asInt() : (d))
#define JSON_ASSTRING_D(j,d) ((j).isString() ? (j).asString() : (d))

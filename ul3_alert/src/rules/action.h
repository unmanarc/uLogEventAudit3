/** @file @brief Validated command, HTTP POST and Telegram actions. */
#pragma once

#include <boost/json.hpp>
#include <Mantids30/Client/HTTPv1/client.h>
#include <string>
#include <vector>

namespace UL3::JSONALERT::Filters {

/** @brief Own a validated action definition; credentials never belong in diagnostics. */
struct Action
{
    boost::json::value originalConfig;

    /** @brief Validate and normalize a definition, preserving omitted secrets on edits. @throws std::invalid_argument Invalid configuration. */
    static Action fromConfig(const boost::json::value &config, const Action *previous = nullptr);
    /** @brief Return a browser-safe definition with credential presence flags. */
    static boost::json::value redact(boost::json::value config);
    /** @brief Render a bounded MantidsLang template with the event as its root context. */
    static std::string render(const std::string &source, const boost::json::value &event);
    /** @brief Build a POST request; Telegram text is JSON-escaped after rendering. */
    Mantids30::Client::HTTPv1::Request request(const boost::json::value &event) const;
    /** @brief Send a POST and validate transport, HTTP status and Telegram's ok flag. */
    bool post(const boost::json::value &event) const;
    /** @brief Execute argv directly with private stdin storage and wait for successful exit. */
    static bool runCommand(const std::string &path, std::vector<std::string> arguments, const std::string &input);
};

} // namespace UL3::JSONALERT::Filters

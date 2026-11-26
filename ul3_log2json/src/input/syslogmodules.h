/** @file @brief Persistent, atomically replaceable syslog parsing modules. */
#pragma once

#include <audit_json.h>
#include <string>
#include <vector>

namespace UL3::LOG2JSON::Input {

/** @brief Compile module filters once and share immutable snapshots with input workers. */
class SyslogModules
{
public:
    /** @brief Load modules from a JSON file; a missing file starts with no modules. */
    static void load(const std::string &path);
    /** @brief Return editable module definitions from the current snapshot. */
    static boost::json::array getConfig();
    /** @brief Validate, persist and activate all modules, or throw without changing them. */
    static void replace(const boost::json::array &modules);
    /** @brief Render one object per module matching both logger and message regexes. */
    static std::vector<boost::json::object> render(const std::string &logger, const std::string &message);
};

} // namespace UL3::LOG2JSON::Input

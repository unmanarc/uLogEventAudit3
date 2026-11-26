/** @file @brief Regressions for configurable filters, template rendering and persistence. */
#include "input/auditlineparser.h"
#include "input/syslogmodules.h"
#include "events/event.h"
#include <Mantids30/DataFormat/JSON/regex_template.h>
#include <boost/regex.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unistd.h>

using namespace UL3::LOG2JSON;

/** @brief Fail the current check with a readable diagnostic. */
static void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

/** @brief Reject a bad configuration while preserving the active module snapshot. */
static void reject(const boost::json::array &configuration, const boost::json::array &expected)
{
    bool rejected = false;
    try
    {
        Input::SyslogModules::replace(configuration);
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    require(rejected, "Invalid configuration accepted");
    require(Input::SyslogModules::getConfig() == expected, "Failed replacement changed active modules");
}

/** @brief Validate quoted and unquoted syntax against the installed libMantids30 renderer. */
static void checkNativeTemplates()
{
    using Mantids30::DataFormat::JSON::RegexTemplate;
    const std::string message = "publickey alice 192.0.2.1 54321";
    const boost::regex pattern(R"((\S+) (\S+) (\S+) ([0-9]+))");
    boost::smatch captures;
    require(boost::regex_match(message, captures, pattern), "Native template fixture did not match");
    const RegexTemplate unquoted(R"json({"source":"sshd","method":(string)<$1>,"user":(string)<$2>,"ip":(string)<$3>,"port":(unsigned)<$4>})json");
    const RegexTemplate quoted(R"json({"source":"sshd","method":"(string)<$1>","user":"(string)<$2>","ip":"(string)<$3>","port":"(unsigned)<$4>"})json");
    const auto result = unquoted.render(captures);
    require(result == quoted.render(captures), "Quotes changed native placeholder rendering");
    require(result.as_object().at("method") == "publickey" && result.as_object().at("user") == "alice" &&
            result.as_object().at("ip") == "192.0.2.1" && result.as_object().at("port").as_uint64() == 54321,
            "Native example did not render its captured values");
    const RegexTemplate literal(R"json({"value":"$1"})json");
    require(literal.placeholderCount() == 0 && literal.render(captures).as_object().at("value") == "$1",
            "Bare capture reference is not literal in libMantids30");
}

/** @brief Verify legacy, forwarded and structured syslog headers share the same extraction. */
static void checkFraming()
{
    Input::SyslogLine line;
    for (const std::string prefix : {
        "<14>Jun 22 12:19:47 host sshd[42]: ",
        "<14><192.0.2.1>Jun 22 12:19:47 host sshd: ",
        "<14>1 2026-11-26T12:19:47Z host sshd 42 auth - ",
        "<14>1 2026-11-26T12:19:47Z host sshd 42 auth [meta x=\"a\\]b\"][other y=\"z\"] "})
    {
        require(Input::parseSyslogLine(prefix + "user=alice count=42", line), "Syslog framing rejected");
        require(line.logger == "sshd" && line.hostName == "host" && line.message == "user=alice count=42",
                "Syslog message boundaries changed");
    }
    require(!Input::parseSyslogLine("not a syslog header", line), "Unframed message accepted");
}

/** @brief Exercise rendering through the common event interface and durable module updates. */
static void checkModules(const std::filesystem::path &directory)
{
    const auto path = directory / "modules.d";
    Input::SyslogModules::load(path.string());
    require(Input::SyslogModules::getConfig().empty(), "Missing file did not start empty");
    const boost::json::object module{
        {"name", "ssh-login"}, {"loggerRegex", "^sshd$"},
        {"messageRegex", "user=(.+) count=([0-9]+)(?: active=(true|false))?"},
        {"jsonTemplate", R"json({"source":"sshd","user":(string)<$1>,"nested":["(string)<$1>",(integer)<$2>,(boolean)<$3>],"missing":(string)<$9>,"$1":"literal","inline":"prefix $1","bare":"$1"})json"}};
    const boost::json::array configuration{module};
    Input::SyslogModules::replace(configuration);
    require(Input::SyslogModules::render("other", "user=alice count=42").empty(), "Logger filter ignored");
    require(Input::SyslogModules::render("sshd", "unmatched").empty(), "Message filter ignored");
    const std::string user = "alice\"\\\nλ";
    const auto events = Input::SyslogModules::render("sshd", "prefix user=" + user + " count=42");
    require(events.size() == 1, "Search match rejected");
    const auto &event = events.front();
    require(event.at("user") == boost::json::value(user), "Capture escaping or Unicode changed");
    require(event.at("nested").as_array()[0] == event.at("user") && event.at("nested").as_array()[1] == 42,
            "Nested or typed capture failed");
    require(!event.at("nested").as_array()[2].as_bool() && event.at("missing") == "", "Capture defaults changed");
    require(event.at("$1") == "literal" && event.at("inline") == "prefix $1" && event.at("bare") == "$1", "Literal text or key replaced");
    require(boost::json::parse(boost::json::serialize(event)) == event, "Capture produced invalid JSON");
    std::unique_ptr<Events::Event> queued = std::make_unique<Events::SyslogEvent>(event);
    require(queued->process() && queued->getJson() == event, "Common event changed rendered JSON");
    require(std::get<0>(queued->getEventId()) > 0 && std::get<2>(queued->getEventId()) > 0, "Missing event identity");

    auto invalid = module;
    invalid["loggerRegex"] = "(";
    reject({invalid}, configuration);
    invalid = module;
    invalid["messageRegex"] = "[";
    reject({invalid}, configuration);
    invalid = module;
    invalid["jsonTemplate"] = "[]";
    reject({invalid}, configuration);
    invalid["jsonTemplate"] = "{broken";
    reject({invalid}, configuration);
    invalid["jsonTemplate"] = R"({"x":(unknown)<$1>})";
    reject({invalid}, configuration);
    reject({module, module}, configuration);
    reject({boost::json::object{{"name", "missing-fields"}}}, configuration);
    reject({42}, configuration);
    Input::SyslogModules::load(path.string());
    require(Input::SyslogModules::getConfig() == configuration, "Reload lost configuration after rejected changes");
    require(Input::SyslogModules::render("sshd", "user=bob count=7 active=true")[0].at("nested").as_array()[2].as_bool(),
            "Optional capture did not become true");

    auto second = module;
    second["name"] = "second";
    Input::SyslogModules::replace({module, second});
    require(Input::SyslogModules::render("sshd", "user=bob count=7").size() == 2, "Matching modules did not each emit");
    Input::SyslogModules::replace({});
    Input::SyslogModules::load(path.string());
    require(Input::SyslogModules::render("sshd", "user=bob count=7").empty(), "Removed modules survived restart");

    // A missing parent directory must leave the active configuration untouched.
    Input::SyslogModules::load((directory / "unwritable" / "modules.d").string());
    reject(configuration, {});
    require(!std::filesystem::exists(directory / "unwritable"), "Failed persistence created a directory");

    // Force the atomic rename to fail after writing and syncing a temporary file.
    Input::SyslogModules::load(path.string());
    Input::SyslogModules::replace(configuration);
    std::filesystem::remove_all(path);
    std::ofstream(path) << "blocked";
    reject({}, configuration);
    for (const auto &entry : std::filesystem::directory_iterator(directory))
    {
        require(entry.path() == path, "Failed rename leaked a temporary file");
    }
}

/** @brief Run all checks in an isolated temporary directory. */
int main()
{
    std::string pattern = "/tmp/ul3-syslog-modules-XXXXXX";
    const auto *created = mkdtemp(pattern.data());
    if (!created)
    {
        return 1;
    }
    const std::filesystem::path directory(created);
    try
    {
        checkNativeTemplates();
        checkFraming();
        checkModules(directory);
        std::filesystem::remove_all(directory);
        std::cout << "PASS: syslog framing, filters, templates, event ownership and atomic configuration\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::filesystem::remove_all(directory);
        std::cerr << error.what() << '\n';
        return 1;
    }
}

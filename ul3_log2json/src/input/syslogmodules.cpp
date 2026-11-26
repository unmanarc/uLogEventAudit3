/** @file @brief Compile filters and libMantids30 templates outside the receive loop. */
#include "syslogmodules.h"
#include "../../../common/config_directory.h"

#include <Mantids30/DataFormat/JSON/regex_template.h>
#include <boost/regex.hpp>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>

namespace UL3::LOG2JSON::Input {
namespace {
using Mantids30::DataFormat::JSON::RegexTemplate;

/** @brief Require a nonempty string field and produce a useful configuration error. */
std::string field(const boost::json::object &object, const char *name)
{
    const auto *value = object.if_contains(name);
    if (!value || !value->is_string() || value->as_string().empty())
    {
        throw std::invalid_argument(std::string("Expected a nonempty string: ") + name);
    }
    return std::string(value->as_string());
}

/** @brief Immutable compiled module, safe to use from concurrent input connections. */
struct Module
{
    boost::regex logger;
    boost::regex message;
    RegexTemplate document;

    /** @brief Compile both filters and the module's object template. */
    explicit Module(const boost::json::object &definition)
        : logger(field(definition, "loggerRegex")), message(field(definition, "messageRegex")),
          document(field(definition, "jsonTemplate"))
    {
        if (!document.base().is_object())
        {
            throw std::invalid_argument("JSON template must produce an object");
        }
    }
};

/** @brief Preserve editable source definitions alongside their compiled representation. */
struct Configuration
{
    boost::json::array definitions;
    std::vector<Module> modules;

    /** @brief Compile the entire configuration before allowing it to replace active modules. */
    explicit Configuration(const boost::json::array &definitions) : definitions(definitions)
    {
        std::set<std::string> names;
        for (const auto &value : definitions)
        {
            if (!value.is_object())
            {
                throw std::invalid_argument("Each module must be an object");
            }
            const auto name = field(value.as_object(), "name");
            try
            {
                if (!names.insert(name).second)
                {
                    throw std::invalid_argument("Duplicate module name");
                }
                modules.emplace_back(value.as_object());
            }
            catch (const std::exception &error)
            {
                throw std::invalid_argument("Module '" + name + "': " + error.what());
            }
        }
    }
};

std::mutex registryMutex;
std::shared_ptr<const Configuration> active = std::make_shared<Configuration>(boost::json::array{});
std::string configPath;

/** @brief Borrow an immutable snapshot without retaining the registry lock while matching. */
std::shared_ptr<const Configuration> snapshot()
{
    std::scoped_lock lock(registryMutex);
    return active;
}

} // namespace

void SyslogModules::load(const std::string &path)
{
    std::scoped_lock lock(registryMutex);
    const auto definitions = UL3::JSON::readConfigDirectory(path);
    auto compiled = std::make_shared<Configuration>(definitions);
    configPath = path;
    active = std::move(compiled);
}

boost::json::array SyslogModules::getConfig()
{
    return snapshot()->definitions;
}

void SyslogModules::replace(const boost::json::array &modules)
{
    auto compiled = std::make_shared<Configuration>(modules);
    std::scoped_lock lock(registryMutex);
    if (configPath.empty())
    {
        throw std::runtime_error("Module configuration path is not initialized");
    }
    UL3::JSON::writeConfigDirectory(configPath, modules);
    active = std::move(compiled);
}

std::vector<boost::json::object> SyslogModules::render(const std::string &logger, const std::string &message)
{
    const auto configuration = snapshot();
    std::vector<boost::json::object> events;
    for (const auto &module : configuration->modules)
    {
        boost::smatch captures;
        if (boost::regex_search(logger, module.logger) && boost::regex_search(message, captures, module.message))
        {
            events.push_back(module.document.render(captures).as_object());
        }
    }
    return events;
}
} // namespace UL3::LOG2JSON::Input

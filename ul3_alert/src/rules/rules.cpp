/**
 * @file
 * @brief Implementation for ul3_alert/src/rules/rules.
 */
#include "rules.h"
#include "../../../common/config_directory.h"
#include <shared_mutex>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/optional.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include "../globals.h"

#include <unistd.h>

#include <cerrno>
#include <string>
#include <thread>

using namespace std;

using namespace UL3::JSONALERT;
using namespace UL3::JSONALERT::Filters;

using namespace Mantids30::Runtime;
using namespace Mantids30::Scripting::JSONExprEval;

vector<unique_ptr<Rule>> Rules::rules;
std::map<std::string, unique_ptr<Action>> Rules::actions;
std::shared_mutex Rules::rulesMutex;
Mantids30::Core::Threads::Safe::Queue<std::string> Rules::evaluationQueue;
uint64_t Rules::queuePushTimeoutMs = 0, Rules::queuePopTimeoutMs = 10000;

std::atomic<uint64_t> Rules::queuedLinesCount, Rules::droppedInQueue;
std::atomic<uint64_t> Rules::badFormatLines, Rules::processedLines;
std::atomic<uint64_t> Rules::rulesActivated;
std::atomic<uint64_t> Rules::queueInLastSecond, Rules::queueOutLastSecond;
std::atomic<uint64_t> Rules::queueInThisPeriod, Rules::queueOutThisPeriod;

std::atomic<double> Rules::evaluationTimeMs;

std::string Rules::rulesFilePath;
std::string Rules::actionsFilePath;

bool Rules::rulesModified = false;
bool Rules::actionsModified = false;

Rules::Rules() = default;

Rules::~Rules()
{
    {
        std::unique_lock<std::shared_mutex> lock(rulesMutex);
        resetRules();
        resetActions();
    }
}

boost::json::object Rules::getStats()
{
    boost::json::object evaluationStats{{"queue", boost::json::object{{"count", static_cast<uint64_t>(evaluationQueue.size())}, {"max", static_cast<uint64_t>(evaluationQueue.getMaxItems())}}},
                                        {"queuedLinesCount", queuedLinesCount.load()},
                                        {"droppedInQueue", droppedInQueue.load()},
                                        {"badFormatLines", badFormatLines.load()},
                                        {"processedLines", processedLines.load()},
                                        {"rulesActivated", rulesActivated.load()},
                                        {"queueInLastSec", queueInLastSecond.load()},
                                        {"queueOutLastSec", queueOutLastSecond.load()},
                                        {"lastEvaluationTimeInMS", evaluationTimeMs.load()}};

    return evaluationStats;
}

bool Rules::startEvaluationThreads(const uint32_t &threadsCount)
{
    if (threadsCount == 0)
    {
        return false;
    }

    queuedLinesCount = 0;
    droppedInQueue = 0;
    badFormatLines = 0;
    processedLines = 0;
    rulesActivated = 0;
    queueInLastSecond = 0;
    queueOutLastSecond = 0;
    queueInThisPeriod = 0;
    queueOutThisPeriod = 0;
    evaluationTimeMs = 0;

    std::thread counterThread = std::thread(runQueueCounters);
    counterThread.detach();

    for (uint32_t index = 0; index < threadsCount; index++)
    {
        std::thread workerThread = std::thread(runEvaluationWorker, index + 1);
        workerThread.detach();
    }
    return true;
}

void Rules::setEvaluationQueueMaxSize(const size_t &queueCapacity)
{
    evaluationQueue.setMaxItems(queueCapacity);
}

bool Rules::enqueueLine(const string &inputText)
{
    auto queuedLine = std::make_shared<std::string>(inputText);
    if (!evaluationQueue.push(queuedLine, queuePushTimeoutMs))
    {
        droppedInQueue++;
        return false;
    }
    queueInThisPeriod++;
    queuedLinesCount++;
    return true;
}

bool Rules::reloadRules()
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);
    boost::json::value root = readRulesFileConfig();
    if (!root.is_array())
    {
        return false;
    }
    resetRules();
    LOG_APP->log0(__func__, Logs::INFO, "Loading filters from file: %s", rulesFilePath.c_str());

    for (uint32_t index = 0; index < Mantids30::DataFormat::JSON::ASARRAY_OR_EMPTY(root).size(); index++)
    {
        addRuleUnlocked(index, root.as_array()[index]);
    }

    rulesModified = false;

    return true;
}

bool Rules::reloadActions()
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);
    const auto root = readActionsFileConfig();
    if (!root.is_array())
    {
        return false;
    }
    std::map<std::string, unique_ptr<Action>> replacement;
    try
    {
        for (const auto &definition : root.as_array())
        {
            auto action = std::make_unique<Action>(Action::fromConfig(definition));
            const auto name = std::string(action->originalConfig.at("name").as_string());
            if (replacement.count(name) != 0)
            {
                return false;
            }
            replacement.emplace(name, std::move(action));
        }
    }
    catch (const std::exception &)
    {
        LOG_APP->log0(__func__, Logs::WARNING, "Invalid action configuration file.");
        return false;
    }
    actions = std::move(replacement);

    actionsModified = false;

    return true;
}

bool Rules::evaluate(const boost::json::value &values)
{
    // Keep rules and referenced actions alive through synchronous process execution.
    std::shared_lock<std::shared_mutex> lock(rulesMutex);

    bool activated = false;
    for (const auto &rule : rules)
    {
        if (rule->expression->evaluate(values))
        {
            activated = true;

            if (rule->ruleAction == RULE_ACTION_EXEC || rule->ruleAction == RULE_ACTION_EXECANDABORT)
            {
                if (actions.find(rule->actionId) == actions.end())
                {
                    LOG_APP->log0(__func__, Logs::ERROR, "Rule '%s' activated, but action '%s' not found", rule->name.c_str(), rule->actionId.c_str());
                }
                else
                {
                    LOG_APP->log0(__func__, Logs::DEBUG, "Rule '%s' activated, executing action '%s'", rule->name.c_str(), rule->actionId.c_str());
                    executeAction(rule->name, *actions.at(rule->actionId), values);
                    if (rule->ruleAction == RULE_ACTION_EXECANDABORT)
                    {
                        break;
                    }
                }
            }
            else if (rule->ruleAction == RULE_ACTION_ABORT)
            {
                LOG_APP->log0(__func__, Logs::DEBUG, "Rule '%s' activated, aborting next rules...", rule->name.c_str());
                break;
            }
        }
    }
    return activated;
}

bool Rules::editRule(uint32_t position, const boost::json::value &configValue)
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);
    return addRuleUnlocked(position, configValue, true);
}

bool Rules::editAction(const boost::json::value &configValue)
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);
    return addActionUnlocked(configValue, true);
}

bool Rules::moveRuleUp(uint32_t position)
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);

    if (position >= rules.size())
    {
        return false;
    }
    if (position == 0)
    {
        return true;
    }

    std::swap(rules[position - 1], rules[position]);

    rulesModified = true;
    return true;
}

bool Rules::moveRuleDown(uint32_t position)
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);

    if (position >= rules.size())
    {
        return false;
    }
    if (position == (rules.size() - 1))
    {
        return true;
    }

    std::swap(rules[position + 1], rules[position]);

    rulesModified = true;
    return true;
}

bool Rules::addRule(uint32_t position, const boost::json::value &configValue)
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);
    return addRuleUnlocked(position, configValue);
}

bool Rules::addAction(const boost::json::value &configValue)
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);
    return addActionUnlocked(configValue);
}

bool Rules::removeRule(const uint32_t &position)
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);
    return removeRuleUnlocked(position);
}

bool Rules::removeAction(const string &actionName)
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);
    return removeActionUnlocked(actionName);
}

void Rules::runEvaluationWorker(const uint32_t &workerId)
{
    std::string threadName = "JSONEVAL";
#ifndef WIN32
    pthread_setname_np(pthread_self(), threadName.c_str());
#endif

    boost::json::value eventValue;
    for (;;)
    {
        auto queuedLine = evaluationQueue.pop(queuePopTimeoutMs);
        if (queuedLine)
        {
            queueOutThisPeriod++;
            auto start = chrono::high_resolution_clock::now();
            auto finish = chrono::high_resolution_clock::now();
            chrono::duration<double, milli> elapsed = finish - start;

            Mantids30::DataFormat::JSON::Reader reader;
            if (reader.parse(*queuedLine, eventValue))
            {
                if (evaluate(eventValue))
                {
                    rulesActivated++;
                }
                processedLines++;
            }
            else
            {
                LOG_APP->log0(__func__, Logs::ERROR, "Evaluation Thread #%d received invalid JSON...", workerId);
                badFormatLines++;
            }

            finish = chrono::high_resolution_clock::now();
            elapsed = finish - start;
            evaluationTimeMs = elapsed.count();
        }
        else
        {
            LOG_APP->log0(__func__, Logs::DEBUG, "Evaluation Thread #%d timed out (no elements)...", workerId);
        }
    }
}

void Rules::runQueueCounters()
{
    std::string threadName = "COUNTERS";
#ifndef WIN32
    pthread_setname_np(pthread_self(), threadName.c_str());
#endif

    for (;;)
    {
        queueInLastSecond = queueInThisPeriod.exchange(0);
        queueOutLastSecond = queueOutThisPeriod.exchange(0);

        sleep(1);
    }
}

void Rules::executeAction(const std::string &ruleName, const Action &action, const boost::json::value &values)
{
    bool success = false;
    try
    {
        const auto &config = action.originalConfig;
        if (Mantids30::DataFormat::JSON::ASSTRING(config, "type", "") == "command")
        {
            std::vector<std::string> arguments;
            for (const auto &argument : Mantids30::DataFormat::JSON::ASARRAY_OR_EMPTY(config, "args"))
            {
                arguments.emplace_back(argument.as_string());
                if (Mantids30::DataFormat::JSON::ASBOOL(config, "appendNewLineToEachArgv", false))
                {
                    arguments.back() += "%N%";
                }
            }
            expandArguments(arguments, ruleName, values);
            success = Action::runCommand(std::string(config.at("path").as_string()), std::move(arguments),
                                        Action::render(std::string(config.at("stdinTemplate").as_string()), values));
        }
        else
        {
            success = action.post(values);
        }
    }
    catch (const std::exception &)
    {
        // Never print exception text, URLs, bodies or arguments: they can contain credentials/event data.
    }
    if (!success)
    {
        LOG_APP->log0(__func__, Logs::WARNING, "Rule '%s': action delivery failed.", ruleName.c_str());
    }
}

void Rules::expandArguments(std::vector<string> &arguments, const std::string &ruleName, const boost::json::value &values)
{
    size_t index = 0;
    for (std::string &argument : arguments)
    {
        if (index != 0)
        {
            expandArgument(argument, ruleName, values);
        }
        index++;
    }
}

void Rules::expandArgument(string &argument, const std::string &ruleName, const boost::json::value &values)
{
    static const boost::regex placeholderPattern(R"(%([^%]*)%)");
    std::string expanded;
    auto start = argument.cbegin();
    for (boost::sregex_iterator placeholder(argument.begin(), argument.end(), placeholderPattern), end; placeholder != end; ++placeholder)
    {
        expanded.append(start, (*placeholder)[0].first);
        expanded += resolvePlaceholder((*placeholder)[1].str(), ruleName, values);
        start = (*placeholder)[0].second;
    }
    expanded.append(start, argument.cend());
    argument = std::move(expanded);
}

string Rules::resolvePlaceholder(const string &placeholder, const string &ruleName, const boost::json::value &values)
{
    if (placeholder.empty())
    {
        return "%";
    }
    else if (placeholder == "N")
    {
        return "\n";
    }
    else if (placeholder.at(0) == '$')
    {
        // The leading marker selects JSON formatting for the resolved path.
        Mantids30::DataFormat::JSON::Path path(placeholder.substr(1));
        const auto *resolved = path.resolve(values);
        const boost::json::value result = resolved ? *resolved : boost::json::value{};
        return UL3::JSON::toStyledString(result);
    }
    else if (placeholder.at(0) == '-')
    {
        // Serialize the resolved value as compact JSON.
        Mantids30::DataFormat::JSON::Path path(placeholder.substr(1));
        const auto *resolved = path.resolve(values);
        const boost::json::value result = resolved ? *resolved : boost::json::value{};
        return Mantids30::DataFormat::JSON::toString(result);
    }
    else if (placeholder.at(0) == '+')
    {
        // Uppercase the compact JSON representation.
        Mantids30::DataFormat::JSON::Path path(placeholder.substr(1));
        const auto *resolved = path.resolve(values);
        const boost::json::value result = resolved ? *resolved : boost::json::value{};
        return boost::to_upper_copy(Mantids30::DataFormat::JSON::toString(result));
    }
    else if (placeholder.at(0) == '#')
    {
        if (placeholder == "#rule.name")
        {
            return ruleName;
        }
        LOG_APP->log0(__func__, Logs::DEBUG, "Invalid Argument Variable '%s' on rule '%s'", placeholder.c_str(), ruleName.c_str());
        return "INVALID_VARGUMENT_VARIABLE";
    }
    else
    {
        LOG_APP->log0(__func__, Logs::DEBUG, "Invalid Argument Variable '%s' on rule '%s'", placeholder.c_str(), ruleName.c_str());
        return "INVALID_VARGUMENT_VARIABLE";
    }
}

bool Rules::addRuleUnlocked(uint32_t position, const boost::json::value &configValue, bool replace)
{
    std::string name = std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "name", ""));
    std::string action = std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "action", ""));

    if (replace)
    {
        if (position >= rules.size())
        {
            LOG_APP->log0(__func__, Logs::WARNING, "Rule '%d': pos not found during replacement.", position);
            return false;
        }
        // TODO: match internal uniqid..
    }

    LOG_APP->log0(__func__, Logs::INFO, "Creating Rule '%s'", name.c_str());

    std::string filter = std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "filter", ""));

    boost::replace_all(filter, "\n", " ");

    auto rule = std::make_unique<Rule>();
    rule->name = name;
    rule->expression = std::make_unique<JSONEval>(filter);
    rule->originalConfig = configValue;

    if (!rule->expression->isCompiled())
    {
        LOG_APP->log0(__func__, Logs::ERROR, "Rule '%s': 'Filter' compilation failed with '%s'.", name.c_str(), rule->expression->getLastCompilerError().c_str());
        return false;
    }

    if (action == "EXEC" || action == "EXECANDABORT")
    {
        rule->ruleAction = action == "EXEC" ? RULE_ACTION_EXEC : RULE_ACTION_EXECANDABORT;
        rule->actionId = std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "actionId", ""));

        if (actions.find(rule->actionId) == actions.end())
        {
            LOG_APP->log0(__func__, Logs::ERROR, "Rule '%s': 'Filter' referenced unexistent action '%s'.", name.c_str(), rule->actionId.c_str());
            return false;
        }
    }
    else if (action == "ABORT")
    {
        rule->ruleAction = RULE_ACTION_ABORT;
    }
    else
    {
    }

    if (!replace)
    {
        if (position >= rules.size())
        {
            rules.push_back(std::move(rule));
        }
        else
        {
            rules.insert(rules.begin() + position, std::move(rule));
        }
    }
    else
    {
        rules[position] = std::move(rule);
    }

    rulesModified = true;
    return true;
}

bool Rules::addActionUnlocked(const boost::json::value &configValue, bool replace)
{
    std::string actionName = std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "name", ""));

    if (!replace)
    {
        if (actions.find(actionName) != actions.end())
        {
            LOG_APP->log0(__func__, Logs::WARNING, "Action '%s': duplicated name.", actionName.c_str());
            return false;
        }
    }
    else
    {
        if (actions.find(actionName) == actions.end())
        {
            LOG_APP->log0(__func__, Logs::WARNING, "Action '%s': name not found during replacement.", actionName.c_str());
            return false;
        }
    }

    std::unique_ptr<Action> action;
    try
    {
        const auto previous = actions.find(actionName);
        action = std::make_unique<Action>(Action::fromConfig(configValue, previous == actions.end() ? nullptr : previous->second.get()));
    }
    catch (const std::exception &)
    {
        LOG_APP->log0(__func__, Logs::WARNING, "Invalid action configuration.");
        return false;
    }

    actions.insert_or_assign(actionName, std::move(action));

    actionsModified = true;
    return true;
}

bool Rules::removeRuleUnlocked(const uint32_t &position)
{
    if (position >= rules.size())
    {
        return false;
    }

    rules.erase(rules.begin() + position);

    rulesModified = true;

    return true;
}

bool Rules::removeActionUnlocked(const string &actionName)
{
    if (actions.find(actionName) == actions.end())
    {
        return false;
    }

    actions.erase(actionName);
    actionsModified = true;

    return true;
}

void Rules::resetRules()
{
    rules.clear();
}

void Rules::resetActions()
{
    actions.clear();
}

boost::json::array Rules::getCurrentRunningActions()
{
    std::shared_lock<std::shared_mutex> lock(rulesMutex);
    boost::json::array result;

    for (const auto &action : actions)
    {
        result.emplace_back(action.second->originalConfig);
    }

    return result;
}

boost::json::value Rules::getCurrentRunningAction(const string &actionName)
{
    std::shared_lock<std::shared_mutex> lock(rulesMutex);
    boost::json::value result;

    if (actions.find(actionName) == actions.end())
    {
        return result;
    }

    return actions.at(actionName)->originalConfig;
}

boost::json::value Rules::getCurrentRunningRule(const uint32_t &position)
{
    std::shared_lock<std::shared_mutex> lock(rulesMutex);
    boost::json::value result;

    if (position >= rules.size())
    {
        return result;
    }

    return rules[position]->originalConfig;
}

boost::json::array Rules::getCurrentRunningRules()
{
    std::shared_lock<std::shared_mutex> lock(rulesMutex);

    boost::json::array result;

    for (const auto &rule : rules)
    {
        result.emplace_back(rule->originalConfig);
    }

    return result;
}

bool Rules::getActionsModified()
{
    std::shared_lock<std::shared_mutex> lock(rulesMutex);
    return actionsModified;
}

bool Rules::getRulesModified()
{
    std::shared_lock<std::shared_mutex> lock(rulesMutex);
    return rulesModified;
}

void Rules::setActionsFilePath(const std::string &value)
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);
    actionsFilePath = value;
}

void Rules::setRulesFilePath(const std::string &value)
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);
    rulesFilePath = value;
}

void Rules::setQueuePopTimeoutMs(const uint64_t &value)
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);
    queuePopTimeoutMs = value;
}

bool Rules::writeRulesFileConfig(const boost::json::value &configValue)
{
    if (!configValue.is_array())
    {
        return false;
    }
    if (readRulesFileConfig() == configValue)
    {
        rulesModified = false;
        return true;
    }

    try
    {
        UL3::JSON::writeConfigDirectory(rulesFilePath, configValue.as_array());
    }
    catch (const std::exception &)
    {
        return false;
    }

    reloadRules();
    rulesModified = false;

    return true;
}

bool Rules::writeActionsFileConfig(const boost::json::value &configValue)
{
    if (!configValue.is_array())
    {
        return false;
    }
    std::unique_lock<std::shared_mutex> lock(rulesMutex);
    std::map<std::string, std::unique_ptr<Action>> replacement;
    boost::json::array normalized;
    try
    {
        for (const auto &definition : configValue.as_array())
        {
            const auto name = std::string(Mantids30::DataFormat::JSON::ASSTRING(definition, "name", ""));
            const auto previous = actions.find(name);
            auto action = std::make_unique<Action>(Action::fromConfig(definition, previous == actions.end() ? nullptr : previous->second.get()));
            if (replacement.count(name) != 0)
            {
                return false;
            }
            normalized.push_back(action->originalConfig);
            replacement.emplace(name, std::move(action));
        }
    }
    catch (const std::exception &)
    {
        return false;
    }
    try
    {
        UL3::JSON::writeConfigDirectory(actionsFilePath, normalized);
    }
    catch (const std::exception &)
    {
        return false;
    }
    actions = std::move(replacement);
    actionsModified = false;
    return true;
}

boost::json::value Rules::readRulesFileConfig()
{
    try
    {
        return UL3::JSON::readConfigDirectory(rulesFilePath);
    }
    catch (const std::exception &)
    {
        return {};
    }
}

boost::json::value Rules::readActionsFileConfig()
{
    try
    {
        return UL3::JSON::readConfigDirectory(actionsFilePath);
    }
    catch (const std::exception &)
    {
        return {};
    }
}

void Rules::setQueuePushTimeoutMs(const uint64_t &value)
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);
    queuePushTimeoutMs = value;
}

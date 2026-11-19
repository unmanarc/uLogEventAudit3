/**
 * @file
 * @brief Implementation for uanlz_alert/src/rules/rules.
 */
#include "rules.h"
#include <shared_mutex>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/optional.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include "../globals.h"

#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <string>
#include <system_error>
#include <thread>

#ifdef _WIN32
#endif

using namespace std;

using namespace UANLZ::JSONALERT;
using namespace UANLZ::JSONALERT::Filters;

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

/**
 * @brief Launch an action with execvp semantics and wait for its child process.
 * @param ruleName Rule name used in launch and wait diagnostics.
 * @param file Executable name or path; a bare name uses PATH lookup.
 * @param arguments Owned argument storage, including the configured argv[0].
 * @note The parent retains argument storage until the child has finished.
 */
void executeProcess(const std::string &ruleName, const char *file, std::vector<string> arguments)
{
    // The strings own argv storage until waitpid returns; execvp borrows these buffers.
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (auto &argument : arguments)
    {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid == -1)
    {
        const auto error = std::error_code(errno, std::generic_category()).message();
        LOG_APP->log0(__func__, Logs::WARNING, "Rule '%s': unable to launch '%s': %s", ruleName.c_str(), file, error.c_str());
    }
    else if (pid == 0)
    {
        execvp(file, argv.data());
        _exit(EXIT_FAILURE);
    }
    else
    {
        int status = 0;
        pid_t result;
        do
        {
            result = waitpid(pid, &status, 0);
        }
        while (result == -1 && errno == EINTR);
        if (result == -1)
        {
            const auto error = std::error_code(errno, std::generic_category()).message();
            LOG_APP->log0(__func__, Logs::WARNING, "Rule '%s': unable to wait for '%s': %s", ruleName.c_str(), file, error.c_str());
        }
        else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG_APP->log0(__func__, Logs::WARNING, "Rule '%s': execution of '%s' failed (wait status %d).", ruleName.c_str(), file, status);
        }
    }
}

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
    resetRules();
    boost::json::value root = readRulesFileConfig();
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
    resetActions();
    boost::json::value root = readActionsFileConfig();

    LOG_APP->log0(__func__, Logs::INFO, "Loading actions from file: %s", actionsFilePath.c_str());
    for (uint32_t index = 0; index < Mantids30::DataFormat::JSON::ASARRAY_OR_EMPTY(root).size(); index++)
    {
        addActionUnlocked(root.as_array()[index]);
    }

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
                    executeAction(rule->name, actions.at(rule->actionId)->file.c_str(), actions.at(rule->actionId)->arguments, values);
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

void Rules::executeAction(const std::string &ruleName, const char *file, std::vector<string> arguments, const boost::json::value &values)
{
    expandArguments(arguments, ruleName, values);
    executeProcess(ruleName, file, arguments);
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
        return UANLZ::JSON::toStyledString(result);
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
    std::string actionDescription = std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "description", ""));

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

    LOG_APP->log0(__func__, Logs::INFO, "Creating Action '%s' - '%s'", actionName.c_str(), actionDescription.c_str());

    auto action = std::make_unique<Action>();
    action->originalConfig = configValue;
    action->setFileName(std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "path", "")));
    vector<string> arguments;
    for (const auto &arg : Mantids30::DataFormat::JSON::ASARRAY_OR_EMPTY(configValue, "args"))
    {
        // Use the same newline placeholder as user-defined action arguments.
        std::string argument = std::string(Mantids30::DataFormat::JSON::ASSTRING_D(arg, ""));
        if (Mantids30::DataFormat::JSON::ASBOOL(configValue, "appendNewLineToEachArgv", false))
        {
            argument += "%N%";
        }
        arguments.push_back(argument);
    }
    action->setArguments(arguments);

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

    std::ofstream configFile(rulesFilePath, std::ifstream::binary);
    if (!configFile.is_open())
    {
        return false;
    }
    configFile << configValue;
    configFile.close();

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
    if (readActionsFileConfig() == configValue)
    {
        actionsModified = false;
        return true;
    }

    std::ofstream configFile(actionsFilePath, std::ifstream::binary);
    if (!configFile.is_open())
    {
        return false;
    }
    configFile << configValue;
    configFile.close();

    reloadActions();
    actionsModified = false;
    return true;
}

boost::json::value Rules::readRulesFileConfig()
{
    boost::json::value root;
    std::ifstream configFile(rulesFilePath, std::ifstream::binary);
    if (!configFile.is_open())
    {
        return root;
    }
    if (!UANLZ::JSON::readConfigArray(configFile, root))
    {
        return {};
    }
    return root;
}

boost::json::value Rules::readActionsFileConfig()
{
    boost::json::value root;
    std::ifstream configFile(actionsFilePath, std::ifstream::binary);
    if (!configFile.is_open())
    {
        return root;
    }
    if (!UANLZ::JSON::readConfigArray(configFile, root))
    {
        return {};
    }
    return root;
}

void Rules::setQueuePushTimeoutMs(const uint64_t &value)
{
    std::unique_lock<std::shared_mutex> lock(rulesMutex);
    queuePushTimeoutMs = value;
}

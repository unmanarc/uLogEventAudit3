/**
 * @file
 * @brief Declarations and contracts for uanlz_alert/src/rules/rules.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <Mantids30/Core/Threads/safe_queue.h>
#include <Mantids30/Scripting/JSONExprEval/jsoneval.h>
#include <shared_mutex>

#include <audit_json.h>
#include <boost/property_tree/ptree.hpp>

namespace UANLZ::JSONALERT::Filters {

/** @brief Select whether a matching rule executes an action or stops further rule evaluation. */
enum RuleAction : std::uint8_t
{
    RULE_ACTION_EXEC,
    RULE_ACTION_ABORT,
    RULE_ACTION_EXECANDABORT,
    RULE_ACTION_UNDEFINED
};

/** @brief Own a compiled Mantids30 expression and the original rule configuration. */
struct Rule
{
    RuleAction ruleAction = RULE_ACTION_UNDEFINED;

    std::string name;
    std::string actionId;

    std::string filter;
    std::unique_ptr<Mantids30::Scripting::JSONExprEval::JSONEval> expression;

    boost::json::value originalConfig;
};

/** @brief Own an executable path and its configured argument vector. */
struct Action
{
    /**
     * @brief Store the executable name or path for this action.
     * @param fileName Executable name or path to store.
     */
    void setFileName(const std::string &fileName) { file = fileName; }

    /**
     * @brief Store action arguments, including the configured argv[0].
     * @param arguments Action argument vector, including the configured argv[0].
     */
    void setArguments(const std::vector<std::string> &arguments) { this->arguments = arguments; }

    std::string file;
    std::vector<std::string> arguments;
    boost::json::value originalConfig;
};

/** @brief Synchronize running rules and actions and consume the Mantids30 evaluation queue. */
class Rules
{
public:
    /**
     * @brief Manage the shared rule engine and its execution queue.
     */
    Rules();
    /**
     * @brief Clear shared rules and actions under the exclusive rules lock.
     */
    ~Rules();

    // Stats:
    /**
     * @brief Return a JSON snapshot of counters and current resource usage.
     */
    static boost::json::object getStats();

    // Threads:
    /**
     * @brief Initialize statistics and start the requested number of JSON rule workers.
     * @param threadsCount Number of consumer workers to start.
     * @return False when the requested worker count is zero; otherwise true.
     */
    static bool startEvaluationThreads(const uint32_t &threadsCount);
    /**
     * @brief Set the number of JSON lines admitted by the Mantids30 evaluation queue.
     * @param queueCapacity Maximum number of pending items admitted by the Mantids30 queue.
     */
    static void setEvaluationQueueMaxSize(const size_t &queueCapacity);
    /**
     * @brief Set the maximum wait for space in the evaluation queue.
     * @param value Maximum queue push wait in milliseconds.
     */
    static void setQueuePushTimeoutMs(const uint64_t &value);
    /**
     * @brief Set the maximum wait for a line in the evaluation queue.
     * @param value Maximum queue pop wait in milliseconds.
     */
    static void setQueuePopTimeoutMs(const uint64_t &value);
    /**
     * @brief Copy a JSON line into Mantids30, transferring ownership only after a successful push.
     * @param inputText Incoming JSON line to copy into the evaluation queue.
     * @return True if the line was queued; false if the queue rejected it.
     */
    static bool enqueueLine(const std::string &inputText);

    // Rule evaluation & Action Triggering:
    /**
     * @brief Evaluate rules in order and execute matching actions until an abort rule stops traversal.
     * @param values Parsed event used for expression evaluation and JSON-path substitutions.
     * @return True if at least one rule matched, regardless of action process exit status.
     */
    static bool evaluate(const boost::json::value &values);

    // Config Rules/Actions:
    /**
     * @brief Compile and replace the rule at the requested position under the rules lock.
     * @param position Zero-based rule or fragment index.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool editRule(uint32_t position, const boost::json::value &configValue);
    /**
     * @brief Replace an existing named action under the rules lock.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool editAction(const boost::json::value &configValue);

    /**
     * @brief Swap a rule with its predecessor when the requested position is valid.
     * @param position Zero-based rule or fragment index.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool moveRuleUp(uint32_t position);
    /**
     * @brief Swap a rule with its successor when the requested position is valid.
     * @param position Zero-based rule or fragment index.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool moveRuleDown(uint32_t position);

    /**
     * @brief Compile and insert a rule, appending if the position is beyond the current list.
     * @param position Zero-based rule or fragment index.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool addRule(uint32_t position, const boost::json::value &configValue);
    /**
     * @brief Register a named action unless its name is already in use.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool addAction(const boost::json::value &configValue);

    /**
     * @brief Remove the rule at the requested position under the rules lock.
     * @param position Zero-based rule or fragment index.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool removeRule(const uint32_t &position);
    /**
     * @brief Remove the named action under the rules lock.
     * @param actionName Identifier of the configured action.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool removeAction(const std::string &actionName);

    /**
     * @brief Select the persisted rule configuration file.
     * @param value Path of the rule configuration file.
     */
    static void setRulesFilePath(const std::string &value);
    /**
     * @brief Select the persisted action configuration file.
     * @param value Path of the action configuration file.
     */
    static void setActionsFilePath(const std::string &value);

    /**
     * @brief Persist an array of rules and reload it when the contents change.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool writeRulesFileConfig(const boost::json::value &configValue);
    /**
     * @brief Persist an array of actions and reload it when the contents change.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool writeActionsFileConfig(const boost::json::value &configValue);

    /**
     * @brief Read the persisted rule array, accepting legacy null as an empty list.
     * @return Configuration array, or JSON null if reading or parsing fails.
     */
    static boost::json::value readRulesFileConfig();
    /**
     * @brief Read the persisted action array, accepting legacy null as an empty list.
     * @return Configuration array, or JSON null if reading or parsing fails.
     */
    static boost::json::value readActionsFileConfig();

    /**
     * @brief Return the original configuration of a rule, or JSON null for an invalid position.
     * @param position Zero-based rule or fragment index.
     * @return Original rule configuration, or JSON null if the position is invalid.
     */
    static boost::json::value getCurrentRunningRule(const uint32_t &position);
    /**
     * @brief Return the original configurations of running rules in evaluation order.
     */
    static boost::json::array getCurrentRunningRules();
    /**
     * @brief Return the original configurations of all registered actions.
     */
    static boost::json::array getCurrentRunningActions();
    /**
     * @brief Return the original configuration of a named action, or JSON null if absent.
     * @param actionName Identifier of the configured action.
     * @return Original action configuration, or JSON null if the name is absent.
     */
    static boost::json::value getCurrentRunningAction(const std::string &actionName);

    /**
     * @brief Report whether running rules have unsaved changes.
     * @return True when running rules are marked as modified.
     */
    static bool getRulesModified();
    /**
     * @brief Report whether running actions have unsaved changes.
     * @return True when running actions are marked as modified.
     */
    static bool getActionsModified();

    /**
     * @brief Replace running rules from the configured file under the rules lock.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool reloadRules();
    /**
     * @brief Replace running actions from the configured file under the rules lock.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool reloadActions();

private:
    // Config Rules/Actions:
    /**
     * @brief Compile and insert or replace a rule while the caller holds the exclusive rules lock.
     * @pre The caller holds rulesMutex exclusively.
     * @param position Zero-based rule or fragment index.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     * @param replace Whether to replace an existing entry rather than insert a new one.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool addRuleUnlocked(uint32_t position, const boost::json::value &configValue, bool replace = false);
    /**
     * @brief Insert or replace an action while the caller holds the exclusive rules lock.
     * @pre The caller holds rulesMutex exclusively.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     * @param replace Whether to replace an existing entry rather than insert a new one.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool addActionUnlocked(const boost::json::value &configValue, bool replace = false);

    /**
     * @brief Erase a rule while the caller holds the exclusive rules lock.
     * @pre The caller holds rulesMutex exclusively.
     * @param position Zero-based rule or fragment index.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool removeRuleUnlocked(const uint32_t &position);
    /**
     * @brief Erase an action while the caller holds the exclusive rules lock.
     * @pre The caller holds rulesMutex exclusively.
     * @param actionName Identifier of the configured action.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool removeActionUnlocked(const std::string &actionName);

    /**
     * @brief Consume JSON lines from Mantids30 and evaluate them for the service lifetime.
     * @note Workers use the existing service-lifetime shutdown model.
     * @param workerId Worker number used in diagnostics.
     */
    static void runEvaluationWorker(const uint32_t &workerId);
    /**
     * @brief Publish and reset enqueue/dequeue counters once per second.
     * @note Workers use the existing service-lifetime shutdown model.
     */
    static void runQueueCounters();

    /**
     * @brief Expand a private copy of configured arguments and execute the action synchronously.
     * @param ruleName Rule name used for logging and rule-name placeholders.
     * @param file Executable name or path passed to execvp.
     * @param arguments Action argument vector, including the configured argv[0].
     * @param values Parsed event used for expression evaluation and JSON-path substitutions.
     */
    static void executeAction(const std::string &ruleName, const char *file, std::vector<std::string> arguments, const boost::json::value &values);
    /**
     * @brief Expand action placeholders in every argument except the configured argv[0].
     * @param arguments Action argument vector, including the configured argv[0].
     * @param ruleName Rule name used for logging and rule-name placeholders.
     * @param values Parsed event used for expression evaluation and JSON-path substitutions.
     */
    static void expandArguments(std::vector<std::string> &arguments, const std::string &ruleName, const boost::json::value &values);
    /**
     * @brief Expand percent-delimited placeholders without recursively expanding inserted values.
     * @param argument One action argument whose placeholders may be expanded in place.
     * @param ruleName Rule name used for logging and rule-name placeholders.
     * @param values Parsed event used for expression evaluation and JSON-path substitutions.
     */
    static void expandArgument(std::string &argument, const std::string &ruleName, const boost::json::value &values);
    /**
     * @brief Resolve a newline, rule name or JSON-path placeholder to its textual value.
     * @param placeholder Placeholder contents without the surrounding percent delimiters.
     * @param ruleName Rule name used for logging and rule-name placeholders.
     * @param values Parsed event used for expression evaluation and JSON-path substitutions.
     */
    static std::string resolvePlaceholder(const std::string &placeholder, const std::string &ruleName, const boost::json::value &values);

    /**
     * @brief Destroy all running rules while the caller holds the exclusive rules lock.
     * @pre The caller holds rulesMutex exclusively.
     */
    static void resetRules();
    /**
     * @brief Destroy all running actions while the caller holds the exclusive rules lock.
     * @pre The caller holds rulesMutex exclusively.
     */
    static void resetActions();

    static uint64_t queuePushTimeoutMs, queuePopTimeoutMs;
    static Mantids30::Core::Threads::Safe::Queue<std::string> evaluationQueue;
    static std::shared_mutex rulesMutex;
    static std::vector<std::unique_ptr<Rule>> rules;
    static std::map<std::string, std::unique_ptr<Action>> actions;

    static std::atomic<uint64_t> queuedLinesCount, droppedInQueue;
    static std::atomic<uint64_t> badFormatLines, processedLines;
    static std::atomic<uint64_t> rulesActivated;
    static std::atomic<uint64_t> queueInLastSecond, queueOutLastSecond;
    static std::atomic<uint64_t> queueInThisPeriod, queueOutThisPeriod;
    static std::atomic<double> evaluationTimeMs;

    static std::string rulesFilePath, actionsFilePath;
    static bool rulesModified, actionsModified;
};

} // namespace UANLZ::JSONALERT::Filters

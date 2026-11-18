#ifndef RULEACTION_H
#define RULEACTION_H

#include <vector>
#include <memory>
#include <string>
#include <mutex>

#include <Mantids30/Scripting/JSONExprEval/jsoneval.h>
#include <shared_mutex>
#include <Mantids30/Core/Threads/safe_queue.h>
#include <memory>

#include <boost/property_tree/ptree.hpp>
#include <audit_json.h>

namespace UANLZ { namespace JSONALERT { namespace Filters {

enum eRuleAction {
    RULE_ACTION_EXEC,
    RULE_ACTION_ABORT,
    RULE_ACTION_EXECANDABORT,
    RULE_ACTION_UNDEFINED
};

struct sRule
{
    eRuleAction ruleAction = RULE_ACTION_UNDEFINED;

    std::string name;
    std::string actionId;

    std::string filter;
    std::unique_ptr<Mantids30::Scripting::JSONExprEval::JSONEval> expr;

    json jOriginalVal;
};

struct sAction
{
    void setFileName(const std::string & sFileName)
    {
        file = sFileName;
    }

    void setArguments(const std::vector<std::string> & vArguments)
    {
        this->vArguments = vArguments;
    }

    std::string file;
    std::vector<std::string> vArguments;
    json jOriginalVal;

};

class Rules
{
public:
    Rules();
    ~Rules();

    ////////////////////////////////////////////////////////////////////////////////
    // Stats:
    static json getStats();

    ////////////////////////////////////////////////////////////////////////////////
    // Threads:
    static bool startEvaluationThreads( const uint32_t & threadsCount );
    static void setEvaluationQueueMaxSize( const size_t & max );
    static void setMaxQueuePushWaitTimeInMilliseconds(const uint64_t &value);
    static void setMaxQueuePopTimeInMilliseconds(const uint64_t &value);
    static bool pushElementOnEvaluationQueue( const std::string & str );

    ////////////////////////////////////////////////////////////////////////////////
    // Rule evaluation & Action Triggering:
    static bool evaluate( const json & values );

    ////////////////////////////////////////////////////////////////////////////////
    // Config Rules/Actions:
    static bool editRule(uint32_t pos, const json & jConfig);
    static bool editAction(const json & jConfig);

    static bool ruleUp(uint32_t pos);
    static bool ruleDown(uint32_t pos);


    static bool addRule(uint32_t pos, const json & jConfig );
    static bool addAction(const json &jConfig);

    static bool removeRule(const uint32_t & pos);
    static bool removeAction(const std::string &actionName);

    static void setRulesFilePath(const std::string &value);
    static void setActionsFilePath(const std::string &value);

    static bool writeRulesFileConfig(const json & jConfig);
    static bool writeActionsFileConfig(const json & jConfig);

    static json readRulesFileConfig();
    static json readActionsFileConfig();

    static json getCurrentRunningRule(const uint32_t &pos);
    static json getCurrentRunningRules();
    static json getCurrentRunningActions();
    static json getCurrentRunningAction(const std::string & sActionName);

    static bool getRulesModified();
    static bool getActionsModified();

    static bool reloadRules();
    static bool reloadActions();


private:

    ////////////////////////////////////////////////////////////////////////////////
    // Config Rules/Actions:
    static bool _addRule(uint32_t pos, const json &jConfig, bool replace=false );
    static bool _addAction(const json &jConfig, bool replace=false);

    static bool _removeRule( const uint32_t & pos );
    static bool _removeAction( const std::string & actionName );

    static void threadEvaluation(const uint32_t & threadId);
    static void threadPassCTStatsEverySecond();

    static void exec(const std::string &ruleName, const char * file, std::vector<std::string> vArguments,const json &values);
    static void replaceArgumentsVars(std::vector<std::string> & vArguments,const std::string &ruleName, const json &values);
    static void replaceArgumentsVar(std::string & vArgument,const std::string &ruleName, const json &values);
    static std::string getValueForVar(const std::string &var, const std::string &ruleName, const json &values);


    static void resetRules();
    static void resetActions();

    static std::string getRandomTag(const std::string & str, uint64_t t);

    static uint64_t maxQueuePushWaitTimeInMilliseconds, maxQueuePopTimeInMilliseconds;
    static Mantids30::Core::Threads::Safe::Queue<std::string> evaluationQueue;
    static std::shared_mutex mtRules;
    static std::vector<std::unique_ptr<sRule>> rules;
    static std::map<std::string, std::unique_ptr<sAction>> actions;

    static std::atomic<uint64_t> queuedLinesCount, droppedInQueue;
    static std::atomic<uint64_t> badFormatLines, processedLines;
    static std::atomic<uint64_t> rulesActivated;
    static std::atomic<uint64_t> queueInLastSec,queueOutLastSec;
    static std::atomic<uint64_t> queueInLastSec_ct,queueOutLastSec_ct;
    static std::atomic<double> evaluationTimeInMS;

    //static json currentRunningRules, currentRunningActions;
    static std::string sRulesFilePath,sActionsFilePath;
    static bool bRulesModified,bActionsModified;

};

}}}

#endif // RULEACTION_H


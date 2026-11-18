#include "rules.h"
#include <shared_mutex>
#include <Mantids30/Core/Helpers/random.h>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <boost/algorithm/string/case_conv.hpp>

#include "../globals.h"

#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <string>
#include <thread>


#ifdef _WIN32
#include <unistd.h>
#endif

using namespace std;


using namespace UANLZ::JSONALERT;
using namespace UANLZ::JSONALERT::Filters;

using namespace Mantids30::Runtime;
using namespace Mantids30::Scripting::JSONExprEval;
using namespace Mantids30::Core::Helpers;


vector<sRule *> Rules::rules;
std::map<std::string,sAction *> Rules::actions;
std::shared_mutex Rules::mtRules;
Mantids30::Core::Threads::Safe::Queue<std::string> Rules::evaluationQueue;
uint64_t Rules::maxQueuePushWaitTimeInMilliseconds=0, Rules::maxQueuePopTimeInMilliseconds=10000;

std::atomic<uint64_t> Rules::queuedLinesCount, Rules::droppedInQueue;
std::atomic<uint64_t> Rules::badFormatLines, Rules::processedLines;
std::atomic<uint64_t> Rules::rulesActivated;
std::atomic<uint64_t> Rules::queueInLastSec,Rules::queueOutLastSec;
std::atomic<uint64_t> Rules::queueInLastSec_ct,Rules::queueOutLastSec_ct;

std::atomic<double> Rules::evaluationTimeInMS;

std::string Rules::sRulesFilePath;
std::string Rules::sActionsFilePath;

bool Rules::bRulesModified = false;
bool Rules::bActionsModified = false;

void forkExec(const std::string & ruleName, const char *file, std::vector<string> vArguments )
{
    char ** argv = (char **)malloc( (vArguments.size()+1)*sizeof(char *) );
    for (size_t i=0; i<vArguments.size();i++)
    {
        argv[i] = (char *)strdup(vArguments[i].c_str());
    }
    argv[vArguments.size()] = 0;

    //pid_t parent = getpid();
    pid_t pid = fork();

    if (pid == -1)
    {
        // error, failed to fork()
    }
    else if (pid > 0)
    {
        // Me...
        int status;
        waitpid(pid, &status, 0);
        if (status!=0)
        {
            char cError[1024]="Unknown Error";

            LOG_APP->log0(__func__,Logs::WARNING,"Rule '%s': execution of '%s' failed with %d (%s).", ruleName.c_str(), string(file).c_str(), status, strerror_r(errno,cError,sizeof(cError)));
        }
    }
    else
    {
        // the child... (process is replaced here)
        execvp(file, argv);
        _exit(EXIT_FAILURE);   // exec never returns
    }

    for (int i=0; argv[i]; i++) free(argv[i]);
    free(argv);
}


Rules::Rules()
{

}

Rules::~Rules()
{
    {
        std::unique_lock<std::shared_mutex> lock(mtRules);
        resetRules();
        resetActions();
    }
}

json Rules::getStats()
{
    boost::json::object j{{"queue", boost::json::object{}}};
    j["queue"].as_object()["count"] = (uint64_t)evaluationQueue.size();
    j["queue"].as_object()["max"] = (uint64_t)evaluationQueue.getMaxItems();

    j["queuedLinesCount"]   = (uint64_t)queuedLinesCount;
    j["droppedInQueue"]     = (uint64_t)droppedInQueue;
    j["badFormatLines"]     = (uint64_t)badFormatLines;
    j["processedLines"]     = (uint64_t)processedLines;
    j["rulesActivated"]     = (uint64_t)rulesActivated;
    j["queueInLastSec"]     = (uint64_t)queueInLastSec;
    j["queueOutLastSec"]    = (uint64_t)queueOutLastSec;
    j["lastEvaluationTimeInMS"] = (double)evaluationTimeInMS;

    return j;
}

bool Rules::startEvaluationThreads(const uint32_t &threadsCount)
{
    if (threadsCount==0) return false;

    queuedLinesCount=0;
    droppedInQueue=0;
    badFormatLines=0;
    processedLines=0;
    rulesActivated=0;
    queueInLastSec=0;
    queueOutLastSec=0;
    queueInLastSec_ct=0;
    queueOutLastSec_ct=0;
    evaluationTimeInMS = 0;

    std::thread t0 = std::thread(threadPassCTStatsEverySecond);
    t0.detach();

    for (uint32_t i=0;i<threadsCount;i++)
    {
        std::thread t = std::thread(threadEvaluation,i+1);
        t.detach();
    }
    return true;
}

void Rules::setEvaluationQueueMaxSize(const size_t &max)
{
    evaluationQueue.setMaxItems(max);
}

bool Rules::pushElementOnEvaluationQueue(const string &str)
{
    std::string * strc = new std::string;
    *strc = str;
    if (!evaluationQueue.push(strc,maxQueuePushWaitTimeInMilliseconds))
    {
        delete strc;
        droppedInQueue++;
        return false;
    }
    queueInLastSec_ct++;
    queuedLinesCount++;
    return true;
}

bool Rules::reloadRules()
{
    std::unique_lock<std::shared_mutex> lock(mtRules);
    resetRules();
    json root = readRulesFileConfig();
    LOG_APP->log0(__func__,Logs::INFO,"Loading filters from file: %s", sRulesFilePath.c_str());

    for (uint32_t i=0;i<Mantids30::DataFormat::JSON::ASARRAY_OR_EMPTY(root).size();i++)
    {
        _addRule(i,root.as_array()[i]);
    }

    bRulesModified = false;

    return true;
}

bool Rules::reloadActions()
{
    std::unique_lock<std::shared_mutex> lock(mtRules);
    resetActions();
    json root = readActionsFileConfig();

    LOG_APP->log0(__func__,Logs::INFO,"Loading actions from file: %s", sActionsFilePath.c_str());
    for (uint32_t i=0;i<Mantids30::DataFormat::JSON::ASARRAY_OR_EMPTY(root).size();i++)
    {
        _addAction(root.as_array()[i]);
    }

    bActionsModified = false;

    return true;
}


bool Rules::evaluate(const json &values)
{
    std::shared_lock<std::shared_mutex> lock(mtRules);

    bool activated = false;
    for (sRule * rule: rules)
    {
        if (rule->expr->evaluate(values))
        {
            // Activate
            activated = true;

            if (rule->ruleAction == RULE_ACTION_EXEC || rule->ruleAction == RULE_ACTION_EXECANDABORT)
            {
                if (actions.find(rule->actionId) == actions.end())
                {
                    LOG_APP->log0(__func__,Logs::ERROR,"Rule '%s' activated, but action '%s' not found", rule->name.c_str(), rule->actionId.c_str());
                }
                else
                {
                    LOG_APP->log0(__func__,Logs::DEBUG,"Rule '%s' activated, executing action '%s'", rule->name.c_str(), rule->actionId.c_str());
                    exec(rule->name, actions[rule->actionId]->file,  actions[rule->actionId]->vArguments,values);
                    if (rule->ruleAction == RULE_ACTION_EXECANDABORT) break;
                }
            }
            else if (rule->ruleAction == RULE_ACTION_ABORT)
            {
                LOG_APP->log0(__func__,Logs::DEBUG,"Rule '%s' activated, aborting next rules...", rule->name.c_str());
                break;
            }
        }
    }
    return activated;
}

bool Rules::editRule(uint32_t pos, const json &jConfig)
{
    std::unique_lock<std::shared_mutex> lock(mtRules);
    return _addRule(pos,jConfig,true);
}

bool Rules::editAction(const json &jConfig)
{
    std::unique_lock<std::shared_mutex> lock(mtRules);
    return _addAction(jConfig,true);
}

bool Rules::ruleUp(uint32_t pos)
{
    std::unique_lock<std::shared_mutex> lock(mtRules);

    if (pos>=rules.size()) return false;
    if (pos==0) return true;

    auto * i = rules[pos-1];
    rules[pos-1] = rules[pos];
    rules[pos] = i;

    bRulesModified = true;
    return true;
}

bool Rules::ruleDown(uint32_t pos)
{
    std::unique_lock<std::shared_mutex> lock(mtRules);

    if (pos>=rules.size()) return false;
    if (pos==(rules.size()-1)) return true;

    auto * i = rules[pos+1];
    rules[pos+1] = rules[pos];
    rules[pos] = i;

    bRulesModified = true;
    return true;
}

bool Rules::addRule(uint32_t pos, const json &jConfig)
{
    std::unique_lock<std::shared_mutex> lock(mtRules);
    return _addRule(pos,jConfig);
}

bool Rules::addAction(const json &jConfig)
{
    std::unique_lock<std::shared_mutex> lock(mtRules);
    return _addAction(jConfig);
}

bool Rules::removeRule(const uint32_t &pos)
{
    std::unique_lock<std::shared_mutex> lock(mtRules);
    return _removeRule(pos);
}

bool Rules::removeAction(const string & actionName)
{
    std::unique_lock<std::shared_mutex> lock(mtRules);
    return _removeAction(actionName);
}

void Rules::threadEvaluation(const uint32_t &threadId)
{
    std::string threadName = "JSONEVAL";
#ifndef WIN32
    pthread_setname_np(pthread_self(), threadName.c_str());
#endif

    json v;
    std::string * s;
    for (;;)
    {
        s = evaluationQueue.pop(maxQueuePopTimeInMilliseconds);
        queueOutLastSec_ct++;
        if (s)
        {
            auto start = chrono::high_resolution_clock::now();
            auto finish = chrono::high_resolution_clock::now();
            chrono::duration<double, milli> elapsed = finish - start;

            Mantids30::DataFormat::JSON::Reader reader;
            if (reader.parse(*s, v))
            {
                if (evaluate(v))
                {
                    rulesActivated++;
                }
                processedLines++;
            }
            else
            {
                LOG_APP->log0(__func__,Logs::ERROR,"Evaluation Thread #%d received invalid JSON...", threadId);
                badFormatLines++;
            }
            delete s;
            s=nullptr;

            finish = chrono::high_resolution_clock::now();
            elapsed = finish - start;
            evaluationTimeInMS = elapsed.count();
        }
        else
        {
            LOG_APP->log0(__func__,Logs::DEBUG,"Evaluation Thread #%d timed out (no elements)...", threadId);
        }
    }
}

void Rules::threadPassCTStatsEverySecond()
{
    std::string threadName = "COUNTERS";
#ifndef WIN32
    pthread_setname_np(pthread_self(), threadName.c_str());
#endif

    for (;;)
    {
        queueInLastSec = (uint64_t)queueInLastSec_ct;
        queueOutLastSec = (uint64_t)queueOutLastSec_ct;

        queueInLastSec_ct = 0;
        queueOutLastSec_ct = 0;

        sleep(1);
    }
}

void Rules::exec(const std::string & ruleName, const char *file, std::vector<string> vArguments,const json &values )
{
    replaceArgumentsVars(vArguments,ruleName,values);
    forkExec(ruleName,file,vArguments);
}

void Rules::replaceArgumentsVars(std::vector<string> &vArguments,const std::string &ruleName,const json &values)
{
    size_t i=0;
    for ( std::string & vArgument : vArguments)
    {
        if (i!=0)
            replaceArgumentsVar(vArgument,ruleName,values);
        i++;
    }
}

void Rules::replaceArgumentsVar(string &vArgument, const std::string &ruleName, const json &values)
{
    boost::match_flag_type flags = boost::match_default;
    // PRECOMPILE _STATIC_TEXT
    std::string secureTag = Random::createRandomString(16);
    uint64_t tagCount=0;
    std::map<std::string,std::string> intermediateTags;

    boost::regex exVar("(?<STATIC_TEXT>\\%[^\\%]*\\%)");
    boost::match_results<string::const_iterator> whatStaticText;
    for (string::const_iterator start = vArgument.begin(), end =  vArgument.end();
         boost::regex_search(start, end, whatStaticText, exVar, flags);
         start = vArgument.begin(), end =  vArgument.end())
    {
        std::string xValue = string(whatStaticText[1].first, whatStaticText[1].second).substr(1);
        xValue.pop_back();
        boost::replace_all(vArgument, string(whatStaticText[1].first, whatStaticText[1].second), getRandomTag(secureTag,tagCount) );
        intermediateTags[ getRandomTag(secureTag,tagCount++) ] = xValue;
    }
    for (const auto & repl : intermediateTags)
    {
        boost::replace_all(vArgument, repl.first, getValueForVar(repl.second,ruleName,values));
    }
}

string Rules::getValueForVar(const string &var, const string &ruleName, const json &values)
{
    if (var.empty()) return "%";

    else if (var == "N")
    {
        return "\n";
    }
    else if (var.at(0) == '$')
    {
        // JSONPATH MULTI-LINE STYLED
        Mantids30::DataFormat::JSON::Path path(var.substr(1));
        const auto *resolved = path.resolve(values);
        const json result = resolved ? *resolved : json{};
        return UANLZ::JSON::toStyledString(result);
    }
    else if (var.at(0) == '-')
    {
        // JSONPATH ONE-LINE UNSTYLED
        Mantids30::DataFormat::JSON::Path path(var.substr(1));
        const auto *resolved = path.resolve(values);
        const json result = resolved ? *resolved : json{};
        return Mantids30::DataFormat::JSON::toString(result);
    }
    else if (var.at(0) == '+')
    {
        // JSONPATH ONE-LINE UNSTYLED MAYUS
        Mantids30::DataFormat::JSON::Path path(var.substr(1));
        const auto *resolved = path.resolve(values);
        const json result = resolved ? *resolved : json{};
        return boost::to_upper_copy(Mantids30::DataFormat::JSON::toString(result));
    }
    else if (var.at(0) == '#')
    {
        if (var.substr(1) == "rule.name")
        {
            return ruleName;
        }
        LOG_APP->log0(__func__,Logs::DEBUG,"Invalid Argument Variable '%s' on rule '%s'", var.c_str(),ruleName.c_str());
        return "INVALID_VARGUMENT_VARIABLE";
    }
    else
    {
        LOG_APP->log0(__func__,Logs::DEBUG,"Invalid Argument Variable '%s' on rule '%s'", var.c_str(),ruleName.c_str());
        return "INVALID_VARGUMENT_VARIABLE";
    }

}

bool Rules::_addRule(uint32_t pos, const json &jConfig, bool replace)
{
    std::string name =  std::string(Mantids30::DataFormat::JSON::ASSTRING(jConfig,"name",""));
    std::string action = std::string(Mantids30::DataFormat::JSON::ASSTRING(jConfig,"action",""));

    if (replace)
    {
        if (pos >= rules.size())
        {
            LOG_APP->log0(__func__,Logs::WARNING,"Rule '%d': pos not found during replacement.",pos);
            return false;
        }
        // TODO: match internal uniqid..
    }

    LOG_APP->log0(__func__,Logs::INFO,"Creating Rule '%s'",name.c_str());

    std::string filter = std::string(Mantids30::DataFormat::JSON::ASSTRING(jConfig,"filter",""));

    boost::replace_all(filter,"\n", " ");

    sRule * rule = new sRule;
    rule->name = name;
    rule->expr = new JSONEval( filter );
    rule->jOriginalVal = jConfig;

    if (!rule->expr->isCompiled())
    {
        LOG_APP->log0(__func__,Logs::ERROR,"Rule '%s': 'Filter' compilation failed with '%s'.",name.c_str(), rule->expr->getLastCompilerError().c_str());
        delete rule;
        return false;
    }

    if (action == "EXEC" || action == "EXECANDABORT")
    {
        rule->ruleAction =  action == "EXEC"? RULE_ACTION_EXEC : RULE_ACTION_EXECANDABORT;
        rule->actionId = std::string(Mantids30::DataFormat::JSON::ASSTRING(jConfig,"actionId",""));
        // TODO: Check if actionId exist...

        if (actions.find(rule->actionId) == actions.end())
        {
            LOG_APP->log0(__func__,Logs::ERROR,"Rule '%s': 'Filter' referenced unexistent action '%s'.",name.c_str(), rule->actionId.c_str());
            delete rule;
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
        if (pos >= rules.size())
            rules.push_back(rule);
        else
            rules.insert( rules.begin()+pos, rule );
    }
    else
    {
        delete rules[pos];
        rules[pos] = rule;
    }


    bRulesModified = true;
    return true;
}

bool Rules::_addAction(const json &jConfig, bool replace)
{
    std::string actionName = std::string(Mantids30::DataFormat::JSON::ASSTRING(jConfig,"name",""));
    std::string actionDescription = std::string(Mantids30::DataFormat::JSON::ASSTRING(jConfig,"description",""));

    if (!replace)
    {
        if (actions.find(actionName) != actions.end())
        {
            LOG_APP->log0(__func__,Logs::WARNING,"Action '%s': duplicated name.",actionName.c_str());
            return false;
        }
    }
    else
    {
        if (actions.find(actionName) == actions.end())
        {
            LOG_APP->log0(__func__,Logs::WARNING,"Action '%s': name not found during replacement.",actionName.c_str());
            return false;
        }
    }

    LOG_APP->log0(__func__,Logs::INFO,"Creating Action '%s' - '%s'",actionName.c_str(), actionDescription.c_str());

    sAction * action = new sAction;
    action->jOriginalVal = jConfig;
    action->setFileName(std::string(Mantids30::DataFormat::JSON::ASSTRING(jConfig,"path","")));
    vector<string> arguments;
    for (const auto &arg : Mantids30::DataFormat::JSON::ASARRAY_OR_EMPTY(jConfig, "args"))
    {
        //AppendNewLineToEarchArgument
        std::string argument = std::string(Mantids30::DataFormat::JSON::ASSTRING_D(arg,""));
        if (Mantids30::DataFormat::JSON::ASBOOL(jConfig,"appendNewLineToEachArgv",false)) argument+="%N%";
        arguments.push_back(argument);
    }
    action->setArguments(arguments);

    if (replace)
    {
        delete actions[actionName];
        actions[actionName] = action;
    }
    else
    {
        actions[actionName] = action;
    }

    bActionsModified = true;
    return true;
}

bool Rules::_removeRule(const uint32_t &pos)
{
    if (pos>=rules.size())
        return false;

    delete rules[pos];
    rules.erase(rules.begin()+pos);

    bRulesModified = true;

    return true;
}

bool Rules::_removeAction(const string &actionName)
{
    if (actions.find(actionName) == actions.end())
        return false;

    delete actions[actionName];
    actions.erase(actionName);
    bActionsModified = true;

    return true;
}

void Rules::resetRules()
{
    for (sRule * rule: rules) delete rule;
    rules.clear();
}

void Rules::resetActions()
{
    for (auto & i : actions) delete i.second;
    actions.clear();
}

string Rules::getRandomTag(const string &str, uint64_t t)
{
    return "__" + str + "[" + std::to_string(t) + "]";
}

json Rules::getCurrentRunningActions()
{
    std::shared_lock<std::shared_mutex> lock(mtRules);
    boost::json::array r;

    for (const auto &action: actions)
        r.emplace_back(action.second->jOriginalVal);

    return r;
}

json Rules::getCurrentRunningAction(const string &sActionName)
{
    std::shared_lock<std::shared_mutex> lock(mtRules);
    json r;

    if (actions.find(sActionName) == actions.end())
        return r;

    return actions[sActionName]->jOriginalVal;
}

json Rules::getCurrentRunningRule(const uint32_t &pos)
{
    std::shared_lock<std::shared_mutex> lock(mtRules);
    json r;

    if (pos>=rules.size())
        return r;

    return rules[pos]->jOriginalVal;
}

json Rules::getCurrentRunningRules()
{
    std::shared_lock<std::shared_mutex> lock(mtRules);

    boost::json::array r;

    for (auto rule: rules) r.emplace_back(rule->jOriginalVal);

    return r;
}

bool Rules::getActionsModified()
{
    std::shared_lock<std::shared_mutex> lock(mtRules);
    return bActionsModified;
}

bool Rules::getRulesModified()
{
    std::shared_lock<std::shared_mutex> lock(mtRules);
    return bRulesModified;
}

void Rules::setActionsFilePath(const std::string &value)
{
    std::unique_lock<std::shared_mutex> lock(mtRules);
    sActionsFilePath = value;
}

void Rules::setRulesFilePath(const std::string &value)
{
    std::unique_lock<std::shared_mutex> lock(mtRules);
    sRulesFilePath = value;
}

void Rules::setMaxQueuePopTimeInMilliseconds(const uint64_t &value)
{
    std::unique_lock<std::shared_mutex> lock(mtRules);
    maxQueuePopTimeInMilliseconds = value;
}

bool Rules::writeRulesFileConfig(const json &jConfig)
{
    if (!jConfig.is_array()) return false;
    if (readRulesFileConfig() == jConfig)
    {
        bRulesModified = false;
        return true;
    }

    std::ofstream config_doc(sRulesFilePath, std::ifstream::binary);
    if (!config_doc.is_open()) return false;
    config_doc << jConfig;
    config_doc.close();

    reloadRules();
    bRulesModified = false;

    return true;
}

bool Rules::writeActionsFileConfig(const json &jConfig)
{
    if (!jConfig.is_array()) return false;
    if (readActionsFileConfig() == jConfig)
    {
        bActionsModified = false;
        return true;
    }

    std::ofstream config_doc(sActionsFilePath, std::ifstream::binary);
    if (!config_doc.is_open()) return false;
    config_doc << jConfig;
    config_doc.close();

    reloadActions();
    bActionsModified = false;
    return true;
}

json Rules::readRulesFileConfig()
{
    json root;
    // Read JSON from file:
    std::ifstream config_doc(sRulesFilePath, std::ifstream::binary);
    if (!config_doc.is_open())
        return root;
    if (!UANLZ::JSON::readConfigArray(config_doc, root)) return {};
    return root;
}

json Rules::readActionsFileConfig()
{
    json root;
    // Read JSON from file:
    std::ifstream config_doc(sActionsFilePath, std::ifstream::binary);
    if (!config_doc.is_open())
        return root;
    if (!UANLZ::JSON::readConfigArray(config_doc, root)) return {};
    return root;
}


void Rules::setMaxQueuePushWaitTimeInMilliseconds(const uint64_t &value)
{
    std::unique_lock<std::shared_mutex> lock(mtRules);
    maxQueuePushWaitTimeInMilliseconds = value;
}

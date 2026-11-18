#include <Mantids30/Runtime/Service/application.h>

#include <Mantids30/Network/Sockets/socket_tcp.h>
#include <Mantids30/Network/Sockets/socket_tls.h>

#include "rpcimpl.h"
#include "globals.h"
#include <config.h>

#include "rules/rules.h"

#include "input/inputs.h"

#include <sys/types.h>

#include <pwd.h>
#include <signal.h>
#include <dirent.h>
#include <unistd.h>

#include <inttypes.h>
#include <sys/stat.h>

using namespace UANLZ::JSONALERT;

using namespace Mantids30::Runtime;
using namespace Mantids30::Runtime::Service;




class Main : public Application
{
public:
    void _shutdown() override
    {
    }

    int _start(int , char *[], Arguments::GlobalArguments *globalArguments) override
    {
        // Start input threads...
        Input::Inputs::startThreads();

        std::thread(RPCImpl::runRPClient).detach();

        LOG_APP->log0(__func__,Logs::INFO,  (globalArguments->getDaemonName() + " initialized with PID: %d").c_str(), getpid());
        return 0;
    }

    void _initvars(int , char *[], Arguments::GlobalArguments * globalArguments) override
    {
        // init variables (pre-config):
        globalArguments->setInifiniteWaitAtEnd(true);

        globalArguments->softwareLicense = "GPLv3 (https://www.gnu.org/licenses/gpl-3.0.en.html)";
        globalArguments->addAuthor({"Aarón Mizrachi","aaron@unmanarc.com"});
        globalArguments->setVersion(atoi(PROJECT_VER_MAJOR), atoi(PROJECT_VER_MINOR), atoi(PROJECT_VER_PATCH), "a");
        globalArguments->softwareDescription = PROJECT_DESCRIPTION;

        globalArguments->addCommandLineOption("Service Options", 'c', "config-dir" , "Configuration directory"  , "/etc/uauditanalyzer/" + globalArguments->getDaemonName(), Mantids30::Core::Memory::Abstract::Var::Type::STRING );
    }

    bool _config(int , char *argv[], Arguments::GlobalArguments * globalArguments) override
    {
        Mantids30::Network::Sockets::Socket_TLS::prepareTLS();

        std::string configDir = globalArguments->getCommandLineOptionValue("config-dir")->toString();

        // process config:
        unsigned int logMode = Logs::Mode::STANDARD;

        Logs::AppLog initLog(Logs::Mode::STANDARD);
        initLog.enableEmptyFieldLogging = true;
        initLog.enableAttributeNameLogging = false;
        initLog.userFieldMinWidth = 1;

        // start program.
        initLog.log(__func__, "","", Logs::INFO, 2048, "Starting... (Build date %s %s), PID: %" PRIi32,__DATE__, __TIME__, getpid());
        initLog.log0(__func__,Logs::INFO, "Using config dir: %s", configDir.c_str());
        initLog.log0(__func__,Logs::INFO, "Loading configuration: %s", (configDir + "/config.ini").c_str());

        boost::property_tree::ptree config_main;

        if (access(configDir.c_str(),R_OK))
        {
            initLog.log0(__func__,Logs::CRITICAL, "Missing configuration dir: %s", configDir.c_str());
            return false;
        }

        chdir(configDir.c_str());

        if (!access((configDir + "/config.ini").c_str(),R_OK))
        {
            struct stat stats;
            // Check file properties...
            stat((configDir + "/config.ini").c_str(), &stats);

            int smode = stats.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);

            if ( smode != 0600 )
            {
                initLog.log0(__func__,Logs::WARNING, "config.ini file permissions are not 0600 and API key may be exposed, changing...");

                if (chmod((configDir + "/config.ini").c_str(),0600))
                {
                    initLog.log0(__func__,Logs::CRITICAL, "Configuration file permissions can't be changed to 0600");
                    return false;
                }
                initLog.log0(__func__,Logs::INFO, "config.ini file permissions changed to 0600");
            }
            boost::property_tree::ini_parser::read_ini( (configDir + "/config.ini").c_str(),config_main);
        }
        else
        {
            initLog.log0(__func__,Logs::CRITICAL, "Missing configuration: %s", (configDir + "/config.ini").c_str());
            return false;
        }

        *(Globals::getConfig_main()) = config_main;

        if ( config_main.get<bool>("Logs.ToSyslog",true) ) logMode|=Logs::Mode::SYSLOG;
        Globals::setAppLog(std::make_unique<Logs::AppLog>(logMode));
        LOG_APP->enableEmptyFieldLogging = true;
        LOG_APP->enableColorLogging = config_main.get<bool>("Logs.ShowColors",true);
        LOG_APP->enableDateLogging = config_main.get<bool>("Logs.ShowDate",true);
        LOG_APP->userFieldMinWidth = 1;
        LOG_APP->enableAttributeNameLogging = false;
        LOG_APP->setDebug(Globals::getConfig_main()->get<bool>("Logs.Debug",false));

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        Input::Inputs::setConfigFilePath(Globals::getConfig_main()->get<std::string>("Input.InputsPath","inputs.json"));
        Filters::Rules::setRulesFilePath(Globals::getConfig_main()->get<std::string>("Filters.RulesPath","rules.json"));
        Filters::Rules::setActionsFilePath(Globals::getConfig_main()->get<std::string>("Filters.ActionsPath","actions.json"));

        Filters::Rules::setEvaluationQueueMaxSize( Globals::getConfig_main()->get<size_t>("Filters.QueueSize",100000) );
        Filters::Rules::setMaxQueuePushWaitTimeInMilliseconds( Globals::getConfig_main()->get<uint64_t>("Filters.QueueMaxPushWaitTimeInMS",0) );
        Filters::Rules::setMaxQueuePopTimeInMilliseconds( Globals::getConfig_main()->get<uint64_t>("Filters.ThreadMaxPopTimeInMS",10000) );
        Filters::Rules::reloadActions(  );
        Filters::Rules::reloadRules(  );
        Filters::Rules::startEvaluationThreads( Globals::getConfig_main()->get<uint32_t>("Filters.Threads",16) );

        if (!Input::Inputs::loadConfig())
        {
            initLog.log0(__func__,Logs::CRITICAL, "Error loading inputs configuration.");
            return false;
        }

        return true;
    }

};

int main(int argc, char *argv[])
{
    Main application;
    return StartApplication(argc, argv, &application);
}


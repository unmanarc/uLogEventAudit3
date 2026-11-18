#include <Mantids30/Runtime/Service/application.h>

#include <Mantids30/Network/Sockets/socket_tcp.h>
#include <Mantids30/Network/Sockets/socket_tls.h>

#include "rpcimpl.h"
#include "globals.h"
#include <config.h>

#include "vars/namedefs.h"

#include "events/events_distributionthreads.h"
#include "events/events_manager.h"
#include "output/outputs.h"
#include "input/inputs.h"

#include <sys/types.h>

#include <pwd.h>
#include <signal.h>
#include <filesystem>
#include <set>
#include <unistd.h>

#include <inttypes.h>
#include <sys/stat.h>

using namespace UANLZ::LOG2JSON;

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
        std::string configDir = globalArguments->getCommandLineOptionValue("config-dir")->toString();

        // start program.
        LOG_APP->log(__func__, "","", Logs::INFO, 2048, "Starting... (Build date %s %s), PID: %" PRIi32,__DATE__, __TIME__, getpid());
        LOG_APP->log0(__func__,Logs::INFO, "Using config dir: %s", configDir.c_str());

        // Start modules here...
        AuditdEvents::Events_Manager::startGC();

        AuditdEvents::Events_DistributionThreads::startDistributionThreads( Globals::getConfig_main()->get<size_t>("Processor.Threads",8) );
        AuditdEvents::Events_DistributionThreads::setQueueSize(Globals::getConfig_main()->get<size_t>("Processor.QueueSize",1000));

        // Start output threads...
        Output::Outputs::startThreads();

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

        // process config:
        unsigned int logMode = Logs::Mode::STANDARD;

        AuditdEvents::NameDefs::init();

        Logs::AppLog initLog(Logs::Mode::STANDARD);
        initLog.enableEmptyFieldLogging = true;
        initLog.enableAttributeNameLogging = false;
        initLog.userFieldMinWidth = 1;

        std::string configDir = globalArguments->getCommandLineOptionValue("config-dir")->toString();

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

        const auto loadDirectory = [&](const std::string &name, auto loadConfig) {
            const auto directory = std::filesystem::path(configDir) / name;
            std::error_code error;
            std::filesystem::directory_iterator iterator(directory, error), end;
            std::set<std::filesystem::path> files;
            while (!error && iterator != end)
            {
                if (iterator->is_regular_file(error)) files.insert(iterator->path());
                if (!error) iterator.increment(error);
            }
            if (error)
            {
                LOG_APP->log0(__func__, Logs::ERROR, "Unable to list configuration directory %s: %s",
                              directory.c_str(), error.message().c_str());
                return false;
            }
            for (const auto &file : files)
            {
                LOG_APP->log0(__func__, Logs::INFO, "Loading configuration from %s", file.c_str());
                loadConfig(file.string());
            }
            return true;
        };

        if (!loadDirectory("receptors.d", Input::Inputs::loadConfig)) return false;
        if (!loadDirectory("dispatchers.d", Output::Outputs::loadConfig)) return false;

        return true;
    }

};

int main(int argc, char *argv[])
{
    Main application;
    return StartApplication(argc, argv, &application);
}

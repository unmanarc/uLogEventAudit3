/**
 * @file
 * @brief Implementation for ul3_log2json/src/main.
 */
#include <Mantids30/Runtime/Service/application.h>

#include <Mantids30/Network/Sockets/socket_tcp.h>
#include <Mantids30/Network/Sockets/socket_tls.h>

#include "globals.h"
#include "rpcimpl.h"
#include <config.h>

#include "vars/namedefs.h"

#include "events/events_distributionthreads.h"
#include "events/events_manager.h"
#include "input/inputs.h"
#include "input/syslogmodules.h"
#include "output/outputs.h"

#include <sys/types.h>

#include <csignal>
#include <pwd.h>
#include <unistd.h>

#include <cinttypes>
#include <sys/stat.h>

using namespace UL3::LOG2JSON;

using namespace Mantids30::Runtime;
using namespace Mantids30::Runtime::Service;

/** @brief Connect the Mantids30 application lifecycle to this executable. */
class Main : public Application
{
public:
    /**
     * @brief Handle the Mantids30 shutdown hook for this executable.
     */
    void _shutdown() override {}

    /**
     * @brief Start this executable's listeners and service-lifetime workers.
     * @param globalArguments Mantids30 service settings and startup arguments.
     * @return Service startup exit status.
     */
    int _start(int, char *[], Arguments::GlobalArguments *globalArguments) override
    {
        std::string configDirectory = globalArguments->getCommandLineOptionValue("config-dir")->toString();

        // start program.
        LOG_APP->log(__func__, "", "", Logs::INFO, 2048, "Starting... (Build date %s %s), PID: %" PRIi32, __DATE__, __TIME__, getpid());
        LOG_APP->log0(__func__, Logs::INFO, "Using config dir: %s", configDirectory.c_str());

        // Start modules here...
        AuditdEvents::EventsManager::startGarbageCollector();

        AuditdEvents::EventDistribution::startDistributionThreads(Globals::getMainConfig()->get<size_t>("Processor.Threads", 8));
        AuditdEvents::EventDistribution::setQueueSize(Globals::getMainConfig()->get<size_t>("Processor.QueueSize", 1000));

        // Start output threads...
        Output::Outputs::startThreads();

        // Start input threads...
        Input::Inputs::startThreads();

        std::thread(RpcImpl::runRpcClient).detach();

        LOG_APP->log0(__func__, Logs::INFO, (globalArguments->getDaemonName() + " initialized with PID: %d").c_str(), getpid());
        return 0;
    }

    /**
     * @brief Configure service identity, lifecycle options and command-line arguments.
     * @param globalArguments Mantids30 service argument registry being initialized.
     */
    void _initvars(int, char *[], Arguments::GlobalArguments *globalArguments) override
    {
        // init variables (pre-config):
        globalArguments->setInifiniteWaitAtEnd(true);

        globalArguments->softwareLicense = "SSPL-1.0 OR LicenseRef-Commercial (https://spdx.org/licenses/SSPL-1.0.html)";
        globalArguments->addAuthor({"Aarón Mizrachi", "aaron@unmanarc.com"});

        globalArguments->setVersion(atoi(PROJECT_VER_MAJOR), atoi(PROJECT_VER_MINOR), atoi(PROJECT_VER_PATCH), "a");
        globalArguments->softwareDescription = PROJECT_DESCRIPTION;

        globalArguments->addCommandLineOption("Service Options",
                                              'c',
                                              "config-dir",
                                              "Configuration directory",
                                              "/etc/ulogeventaudit3/" + globalArguments->getDaemonName(),
                                              Mantids30::Core::Memory::Abstract::Var::Type::STRING);
    }

    /**
     * @brief Load service configuration and initialize resources before worker startup.
     * @param globalArguments Parsed Mantids30 arguments selecting the configuration directory.
     * @return True when configuration and resource initialization succeed.
     */
    bool _config(int, char *[], Arguments::GlobalArguments *globalArguments) override
    {
        Mantids30::Network::Sockets::Socket_TLS::prepareTLS();

        // process config:
        unsigned int logMode = Logs::Mode::STANDARD;

        AuditdEvents::NameDefs::init();

        Logs::AppLog initLog(Logs::Mode::STANDARD);
        initLog.enableEmptyFieldLogging = true;
        initLog.enableAttributeNameLogging = false;
        initLog.userFieldMinWidth = 1;

        std::string configDirectory = globalArguments->getCommandLineOptionValue("config-dir")->toString();

        initLog.log0(__func__, Logs::INFO, "Loading configuration: %s", (configDirectory + "/config.ini").c_str());

        boost::property_tree::ptree mainConfig;

        if (access(configDirectory.c_str(), R_OK))
        {
            initLog.log0(__func__, Logs::CRITICAL, "Missing configuration dir: %s", configDirectory.c_str());
            return false;
        }

        chdir(configDirectory.c_str());

        if (!access((configDirectory + "/config.ini").c_str(), R_OK))
        {
            struct stat fileStatus{};
            // Check file properties...
            if (stat((configDirectory + "/config.ini").c_str(), &fileStatus) != 0)
            {
                initLog.log0(__func__, Logs::CRITICAL, "Unable to read configuration file properties");
                return false;
            }

            const mode_t filePermissions = fileStatus.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);

            if (filePermissions != 0600)
            {
                initLog.log0(__func__, Logs::WARNING, "config.ini file permissions are not 0600 and API key may be exposed, changing...");

                if (chmod((configDirectory + "/config.ini").c_str(), 0600))
                {
                    initLog.log0(__func__, Logs::CRITICAL, "Configuration file permissions can't be changed to 0600");
                    return false;
                }
                initLog.log0(__func__, Logs::INFO, "config.ini file permissions changed to 0600");
            }

            boost::property_tree::ini_parser::read_ini(configDirectory + "/config.ini", mainConfig);
        }
        else
        {
            initLog.log0(__func__, Logs::CRITICAL, "Missing configuration: %s", (configDirectory + "/config.ini").c_str());
            return false;
        }

        *(Globals::getMainConfig()) = mainConfig;

        if (mainConfig.get<bool>("Logs.ToSyslog", true))
        {
            logMode |= Logs::Mode::SYSLOG;
        }
        Globals::setAppLog(std::make_unique<Logs::AppLog>(logMode));
        LOG_APP->enableEmptyFieldLogging = true;
        LOG_APP->enableColorLogging = mainConfig.get<bool>("Logs.ShowColors", true);
        LOG_APP->enableDateLogging = mainConfig.get<bool>("Logs.ShowDate", true);
        LOG_APP->userFieldMinWidth = 1;
        LOG_APP->enableAttributeNameLogging = false;
        LOG_APP->setDebug(Globals::getMainConfig()->get<bool>("Logs.Debug", false));

        try
        {
            Input::SyslogModules::load(mainConfig.get<std::string>("Syslog.ModulesFile", "syslog_modules.d"));
        }
        catch (const std::exception &error)
        {
            LOG_APP->log0(__func__, Logs::CRITICAL, "Unable to load syslog modules: %s", error.what());
            return false;
        }

        try
        {
            Input::Inputs::loadConfig(mainConfig.get<std::string>("Inputs.File", "inputs.d"));
            Output::Outputs::loadConfig(mainConfig.get<std::string>("Outputs.File", "outputs.d"));
        }
        catch (const std::exception &error)
        {
            LOG_APP->log0(__func__, Logs::CRITICAL, "Unable to load TCP endpoints: %s", error.what());
            return false;
        }

        return true;
    }
};

/**
 * @brief Run this executable through the Mantids30 application lifecycle.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector owned by the runtime.
 * @return Exit status returned by Mantids30.
 */
int main(int argc, char *argv[])
{
    Main application;
    return StartApplication(argc, argv, &application);
}

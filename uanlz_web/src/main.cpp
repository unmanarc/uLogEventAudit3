/**
 * @file
 * @brief Implementation for uanlz_web/src/main.
 */
#include "globals.h"
#include "rpcserverimpl.h"
#include "webserverimpl.h"
#include <Mantids30/ConfigBuilder/Runtime_Logs/config_builder.h>
#include <Mantids30/Network/Sockets/socket_tls.h>
#include <Mantids30/Runtime/Service/application.h>
#include <Mantids30/Runtime/Service/program_config.h>
#include <config.h>
#include <filesystem>
using namespace Mantids30;
using namespace Runtime::Service;
using namespace UANLZ::WEB;
/** @brief Connect the Mantids30 application lifecycle to this executable. */
class Main : public Application
{
public:
    /**
     * @brief Configure service identity, lifecycle options and command-line arguments.
     * @param globalArguments Mantids30 service argument registry being initialized.
     */
    void _initvars(int, char *[], Arguments::GlobalArguments *globalArguments) override
    {
        globalArguments->setInifiniteWaitAtEnd(true);
        globalArguments->softwareLicense = "GPLv3";
        globalArguments->softwareDescription = PROJECT_DESCRIPTION;
        globalArguments->addAuthor({"Aarón Mizrachi", "aaron@unmanarc.com"});
        globalArguments->setVersion(atoi(PROJECT_VER_MAJOR), atoi(PROJECT_VER_MINOR), atoi(PROJECT_VER_PATCH), "stable");
        globalArguments->addCommandLineOption("Service", 'c', "config-dir", "Configuration directory", "/etc/uauditanalyzer3/uanlz_web", Core::Memory::Abstract::Var::Type::STRING);
    }
    /**
     * @brief Load service configuration and initialize resources before worker startup.
     * @param globalArguments Parsed Mantids30 arguments selecting the configuration directory.
     * @return True when configuration and resource initialization succeed.
     */
    bool _config(int, char *[], Arguments::GlobalArguments *globalArguments) override
    {
        Network::Sockets::Socket_TLS::prepareTLS();
        auto configDirectory = std::filesystem::absolute(globalArguments->getCommandLineOptionValue("config-dir")->toString());
        auto initialLog = ConfigBuilder::Runtime_Logs::Builder::createInitLog();
        auto config = Config::Loader::loadSecureApplicationConfig(initialLog.get(), configDirectory.c_str(), "webserver.conf");
        if (!config)
        {
            return false;
        }
        Globals::config = *config;
        LOG_APP = ConfigBuilder::Runtime_Logs::Builder::createAppLog(*config);
        LOG_RPC = ConfigBuilder::Runtime_Logs::Builder::createRPCLog(*config);
        std::filesystem::current_path(configDirectory);
        return true;
    }
    /**
     * @brief Start this executable's listeners and service-lifetime workers.
     * @param globalArguments Mantids30 service settings and startup arguments.
     * @return Service startup exit status.
     */
    int _start(int, char *[], Arguments::GlobalArguments *globalArguments) override
    {
        if (!RpcServerImpl::createRpcListener() || !WebServerImpl::createWebServer())
        {
            globalArguments->setInifiniteWaitAtEnd(false);
            return EXIT_FAILURE;
        }
        LOG_APP->log0(__func__, Runtime::Logs::INFO, "uAuditAnalyzer3 REST service ready");
        return EXIT_SUCCESS;
    }
    /**
     * @brief Handle the Mantids30 shutdown hook for this executable.
     */
    void _shutdown() override
    {
        Globals::webServer.reset();
        if (Globals::fastRpc)
        {
            Globals::fastRpc->stop();
        }
        if (Globals::rpcListener)
        {
            Globals::rpcListener->stop();
        }
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

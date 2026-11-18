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
class Main : public Application
{
public:
    void _initvars(int, char *[], Arguments::GlobalArguments *args) override
    {
        args->setInifiniteWaitAtEnd(true);
        args->softwareLicense = "GPLv3";
        args->softwareDescription = PROJECT_DESCRIPTION;
        args->addAuthor({"Aarón Mizrachi", "aaron@unmanarc.com"});
        args->setVersion(atoi(PROJECT_VER_MAJOR), atoi(PROJECT_VER_MINOR), atoi(PROJECT_VER_PATCH), "stable");
        args->addCommandLineOption("Service", 'c', "config-dir", "Configuration directory", "/etc/uauditanalyzer/uanlz_web", Core::Memory::Abstract::Var::Type::STRING);
    }
    bool _config(int, char *[], Arguments::GlobalArguments *args) override
    {
        Network::Sockets::Socket_TLS::prepareTLS();
        auto dir = std::filesystem::absolute(args->getCommandLineOptionValue("config-dir")->toString());
        auto log = ConfigBuilder::Runtime_Logs::Builder::createInitLog();
        auto config = Config::Loader::loadSecureApplicationConfig(log.get(), dir.c_str(), "webserver.conf");
        if (!config)
            return false;
        Globals::config = *config;
        LOG_APP = ConfigBuilder::Runtime_Logs::Builder::createAppLog(*config);
        LOG_RPC = ConfigBuilder::Runtime_Logs::Builder::createRPCLog(*config);
        std::filesystem::current_path(dir);
        return true;
    }
    int _start(int, char *[], Arguments::GlobalArguments *args) override
    {
        if (!RPCServerImpl::createRPCListener() || !WebServerImpl::createWebServer())
        {
            args->setInifiniteWaitAtEnd(false);
            return EXIT_FAILURE;
        }
        LOG_APP->log0(__func__, Runtime::Logs::INFO, "uAuditAnalyzer3 REST service ready");
        return EXIT_SUCCESS;
    }
    void _shutdown() override
    {
        Globals::webServer.reset();
        if (Globals::fastRPC)
            Globals::fastRPC->stop();
        if (Globals::rpcListener)
            Globals::rpcListener->stop();
    }
};
int main(int argc, char *argv[])
{
    Main application;
    return StartApplication(argc, argv, &application);
}

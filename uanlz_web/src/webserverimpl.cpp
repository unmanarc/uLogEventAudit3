#include "webserverimpl.h"
#include "globals.h"
#include <Mantids30/Client/APISync/apisync.h>
#include <Mantids30/ConfigBuilder/Server_WebRESTFul/config_builder.h>
#include <config.h>
#include <cstdlib>
using namespace Mantids30;
using namespace UANLZ::WEB;
using namespace Server::WebRESTFul;
using namespace Protocol::HTTP;
namespace {
struct Route
{
    Method verb;
    const char *path;
    const char *target;
    const char *method;
};
Route routes[] = {
#include "rest_routes.inc"
};
APIReturn sessionInfo(void *, const RESTful::RequestContext &request, Sessions::ClientDetails &)
{
    APIReturn result(boost::json::object{{"user", request.jwtToken->getSubject()},
                                         {"csrfToken", request.jwtToken->getJwtId()},
                                         {"maxAge", request.jwtToken->getExpirationTime() - time(nullptr)},
                                         {"version", PROJECT_VER}});
    result.httpExtraHeaders["Cache-Control"] = "no-store";
    return result;
}
APIReturn forward(void *context, const RESTful::RequestContext &request, Sessions::ClientDetails &client)
{
    const auto &route = *static_cast<Route *>(context);
    if (route.verb != Method::GET && (request.jwtToken->getJwtId().empty() || request.clientRequest->headers.getOptionValueStringByName("X-CSRF-Token") != request.jwtToken->getJwtId()))
        return APIReturn(Status::Code::S_403_FORBIDDEN, "invalid_csrf_token", "Refresh the audit session and try again");
    if (!request.inputJSON->is_null() && !request.inputJSON->is_object() && !request.inputJSON->is_array())
        return APIReturn(Status::Code::S_400_BAD_REQUEST, "invalid_request", "Expected a JSON object or array");
    if (!Globals::fastRPC->checkConnectionKey(route.target))
        return APIReturn(Status::Code::S_503_SERVICE_UNAVAILABLE, "service_unavailable", "Audit service is disconnected");
    boost::json::value error;
    auto result = Globals::fastRPC->runRemoteRPCMethod(route.target, route.method, *request.inputJSON, &error, false);
    if (!DataFormat::JSON::ASBOOL(error, "succeed", false))
    {
        LOG_APP->log2(__func__, request.jwtToken->getSubject(), client.ipAddress, Runtime::Logs::ERROR, "Backend call failed: %s", route.method);
        return APIReturn(Status::Code::S_502_BAD_GATEWAY, "backend_error", "Audit service could not complete the request");
    }
    APIReturn response(result);
    response.httpExtraHeaders["Cache-Control"] = "no-store";
    return response;
}
} // namespace
bool WebServerImpl::createWebServer()
{
    const auto section = Globals::config.get_child_optional("WebService");
    if (!section || !section->get_child_optional("APISync"))
    {
        LOG_APP->log0(__func__, Runtime::Logs::CRITICAL, "Missing WebService or APISync configuration");
        return false;
    }
    const char *envKey = std::getenv("UAUDIT_API_KEY");
    const char *envApp = std::getenv("UAUDIT_APP");
    const std::string key = envKey ? envKey : Globals::config.get<std::string>("Authentication.APIKey", "");
    const std::string app = envApp ? envApp : Globals::config.get<std::string>("Authentication.AppName", "UAUDITANALYZER");
    if (key.empty() || key.find("REPLACEME") == 0 || app.empty())
    {
        LOG_APP->log0(__func__, Runtime::Logs::CRITICAL, "Configure UAUDIT_API_KEY for uFastAuthD3 APISync");
        return false;
    }
    std::map<std::string, std::string> vars{{"APIKEY", key}, {"APP", app}};
    Globals::webServer.reset(
        ConfigBuilder::Server_WebRESTFul::Builder::createRESTfulEngine(*section, LOG_APP, LOG_RPC, app, "/var/www/uauditweb", ConfigBuilder::Server_WebRESTFul::REST_ENGINE_MANDATORY_SSL, vars));
    if (!Globals::webServer)
        return false;
    Client::APISync::APISyncParameters sync;
    sync.loadFromInfoTree(section->get_child("APISync"));
    auto syncResult = Client::APISync::updateAccessControlContext(
        LOG_APP.get(),
        &sync,
        app,
        key,
        boost::json::parse(R"([{"id":"stats","description":"Read audit statistics and configuration"},{"id":"control","description":"Modify audit rules, actions and inputs"}])"),
        boost::json::parse(R"([{"id":"AUDIT_READER","description":"Audit reader","scopes":["stats"]},{"id":"AUDIT_ADMIN","description":"Audit administrator","scopes":["stats","control"]}])"),
        boost::json::parse(R"([{"id":"LOGIN","description":"Audit console login","parentActivity":null}])"));
    if (!syncResult.is_object())
    {
        LOG_APP->log0(__func__, Runtime::Logs::CRITICAL, "uFastAuthD3 access control synchronization failed");
        return false;
    }
    auto endpoints = std::make_shared<RESTful::Endpoints>();
    endpoints->addEndpoint(Method::GET, "session", Security::JWT_COOKIE_AUTH, {}, nullptr, &sessionInfo);
    for (auto &route : routes)
    {
        const std::string scope = route.verb == Method::GET ? "stats" : "control";
        endpoints->addEndpoint(route.verb, route.path, Security::JWT_COOKIE_AUTH, {scope}, &route, &forward);
    }
    auto &engine = Globals::webServer;
    engine->config.appName = app;
    engine->config.setSoftwareVersion(atoi(PROJECT_VER_MAJOR), atoi(PROJECT_VER_MINOR), atoi(PROJECT_VER_PATCH), "stable");
    engine->endpointsHandler[1] = endpoints;
    engine->startInBackground();
    return true;
}

/**
 * @file
 * @brief Implementation for ul3_web/src/webserverimpl.
 */
#include "webserverimpl.h"
#include "globals.h"
#include "resource_sensing.h"
#include <Mantids30/Client/APISync/apisync.h>
#include <Mantids30/ConfigBuilder/Server_WebRESTFul/config_builder.h>
#include <boost/algorithm/string/predicate.hpp>
#include <config.h>
#include <cstdlib>
#include <future>
using namespace Mantids30;
using namespace UL3::WEB;
using namespace Server::WebRESTFul;
using namespace Protocol::HTTP;
namespace {
/** @brief Fixed mapping from an HTTP endpoint to an allowed backend method. */
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
/** @brief Fixed supported resource resolutions; callers cannot choose arbitrary RPC targets. */
std::string resourceResolutions[] = {"3s", "1m", "60m"};

/** @brief Fetch a remote process history while isolating disconnected or failed services. */
boost::json::object remoteResources(const char *target, const std::string &resolution)
{
    if (!Globals::fastRpc->checkConnectionKey(target))
    {
        return {{"available", false}, {"error", "service_disconnected"}};
    }
    boost::json::value error;
    auto result = Globals::fastRpc->runRemoteRPCMethod(target, "stats.ResourceMonitor",
                                                      boost::json::object{{"resolution", resolution}}, &error, false);
    if (!DataFormat::JSON::ASBOOL(error, "succeed", false) || !result.is_object()
        || !result.as_object().if_contains("samples"))
    {
        return {{"available", false}, {"error", "resource_sensing_unavailable"}};
    }
    auto history = std::move(result.as_object());
    history["available"] = true;
    return history;
}

/** @brief Combine three independent process histories under the authenticated stats scope. */
APIReturn resourceOverview(void *context, const RESTful::RequestContext &, Sessions::ClientDetails &)
{
    const auto &resolution = *static_cast<std::string *>(context);
    // Parallel calls bound the wait to one RPC timeout if both peers are unresponsive.
    auto alerts = std::async(std::launch::async, remoteResources, "ALERTS", resolution);
    auto log2json = std::async(std::launch::async, remoteResources, "LOG2JSON", resolution);
    auto web = UL3::ResourceSensing::instance().history(resolution);
    web["available"] = true;
    APIReturn response(boost::json::object{
        {"resolution", resolution},
        {"components", boost::json::object{{"web", std::move(web)},
                                            {"alerts", alerts.get()}, {"log2json", log2json.get()}}}});
    response.httpExtraHeaders["Cache-Control"] = "no-store";
    return response;
}
/**
 * @brief Return authenticated session metadata and prevent response caching.
 * @param request Mantids30 request with a validated JWT token.
 * @return Subject, CSRF token, remaining token lifetime and software version.
 */
APIReturn sessionInfo(void *, const RESTful::RequestContext &request, Sessions::ClientDetails &)
{
    APIReturn result(boost::json::object{{"user", request.jwtToken->getSubject()},
                                         {"csrfToken", request.jwtToken->getJwtId()},
                                         {"maxAge", request.jwtToken->getExpirationTime() - time(nullptr)},
                                         {"version", PROJECT_VER}});
    result.httpExtraHeaders["Cache-Control"] = "no-store";
    return result;
}
/**
 * @brief Forward an authenticated REST operation using its fixed route-table entry.
 * @param context Borrowed Route entry registered when constructing the endpoints.
 * @param request Authenticated request containing the JSON payload and session token.
 * @param client Client address used in backend failure diagnostics.
 * @return Backend result, or an HTTP error for CSRF, payload, connection or RPC failures.
 */
APIReturn forward(void *context, const RESTful::RequestContext &request, Sessions::ClientDetails &client)
{
    // Only the fixed route table selects backend names and methods; request data is payload.
    const auto &route = *static_cast<Route *>(context);
    // JWT authentication identifies the session; mutations also bind to its CSRF token.
    if (route.verb != Method::GET && (request.jwtToken->getJwtId().empty() || request.clientRequest->headers.getOptionValueStringByName("X-CSRF-Token") != request.jwtToken->getJwtId()))
    {
        return {Status::Code::S_403_FORBIDDEN, "invalid_csrf_token", "Refresh the audit session and try again"};
    }
    if (!request.inputJSON->is_null() && !request.inputJSON->is_object() && !request.inputJSON->is_array())
    {
        return {Status::Code::S_400_BAD_REQUEST, "invalid_request", "Expected a JSON object or array"};
    }
    if (!Globals::fastRpc->checkConnectionKey(route.target))
    {
        return {Status::Code::S_503_SERVICE_UNAVAILABLE, "service_unavailable", "Audit service is disconnected"};
    }
    boost::json::value rpcError;
    auto result = Globals::fastRpc->runRemoteRPCMethod(route.target, route.method, *request.inputJSON, &rpcError, false);
    if (!DataFormat::JSON::ASBOOL(rpcError, "succeed", false))
    {
        LOG_APP->log2(__func__, request.jwtToken->getSubject(), client.ipAddress, Runtime::Logs::ERROR, "Backend call failed: %s", route.method);
        return {Status::Code::S_502_BAD_GATEWAY, "backend_error", "Audit service could not complete the request"};
    }
    APIReturn response(result);
    response.httpExtraHeaders["Cache-Control"] = "no-store";
    return response;
}
} // namespace
bool WebServerImpl::createWebServer()
{
    const auto webConfig = Globals::config.get_child_optional("WebService");
    if (!webConfig || !webConfig->get_child_optional("APISync"))
    {
        LOG_APP->log0(__func__, Runtime::Logs::CRITICAL, "Missing WebService or APISync configuration");
        return false;
    }
    const char *apiKeyEnvironment = std::getenv("UL3_API_KEY");
    const char *appNameEnvironment = std::getenv("UL3_APP");
    const std::string apiKey = apiKeyEnvironment ? apiKeyEnvironment : Globals::config.get<std::string>("Authentication.APIKey", "");
    const std::string appName = appNameEnvironment ? appNameEnvironment : Globals::config.get<std::string>("Authentication.AppName", "ULOGEVENTAUDIT3");
    if (apiKey.empty() || boost::starts_with(apiKey, "REPLACEME") || appName.empty())
    {
        LOG_APP->log0(__func__, Runtime::Logs::CRITICAL, "Configure UL3_API_KEY for uFastAuthD3 APISync");
        return false;
    }
    std::map<std::string, std::string> templateVariables{{"APIKEY", apiKey}, {"APP", appName}};
    Globals::webServer.reset(ConfigBuilder::Server_WebRESTFul::Builder::createRESTfulEngine(*webConfig,
                                                                                            LOG_APP,
                                                                                            LOG_RPC,
                                                                                            appName,
                                                                                            "/var/www/ulogeventaudit3/webroot",
                                                                                            ConfigBuilder::Server_WebRESTFul::REST_ENGINE_MANDATORY_SSL,
                                                                                            templateVariables));
    if (!Globals::webServer)
    {
        return false;
    }
    Client::APISync::APISyncParameters syncParameters;
    syncParameters.loadFromInfoTree(webConfig->get_child("APISync"));
    auto syncResult = Client::APISync::updateAccessControlContext(
        LOG_APP.get(),
        &syncParameters,
        appName,
        apiKey,
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
    for (auto &resolution : resourceResolutions)
    {
        endpoints->addEndpoint(Method::GET, "resources/" + resolution, Security::JWT_COOKIE_AUTH,
                               {"stats"}, &resolution, &resourceOverview);
    }
    for (auto &route : routes)
    {
        const std::string scope = route.verb == Method::GET ? "stats" : "control";
        endpoints->addEndpoint(route.verb, route.path, Security::JWT_COOKIE_AUTH, {scope}, &route, &forward);
    }
    auto &engine = Globals::webServer;
    engine->config.appName = appName;
    engine->config.setSoftwareVersion(atoi(PROJECT_VER_MAJOR), atoi(PROJECT_VER_MINOR), atoi(PROJECT_VER_PATCH), "stable");
    engine->endpointsHandler[1] = endpoints;
    engine->startInBackground();
    return true;
}

/**
 * @file
 * @brief Implementation for uanlz_web/src/webserverimpl.
 */
#include "webserverimpl.h"
#include "globals.h"
#include <boost/algorithm/string/predicate.hpp>
#include <Mantids30/Client/APISync/apisync.h>
#include <Mantids30/ConfigBuilder/Server_WebRESTFul/config_builder.h>
#include <config.h>
#include <cstdlib>
using namespace Mantids30;
using namespace UANLZ::WEB;
using namespace Server::WebRESTFul;
using namespace Protocol::HTTP;
namespace
{
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
    if (route.verb != Method::GET &&
        (request.jwtToken->getJwtId().empty() ||
         request.clientRequest->headers.getOptionValueStringByName("X-CSRF-Token") != request.jwtToken->getJwtId()))
        return APIReturn(Status::Code::S_403_FORBIDDEN, "invalid_csrf_token",
                         "Refresh the audit session and try again");
    if (!request.inputJSON->is_null() && !request.inputJSON->is_object() && !request.inputJSON->is_array())
        return APIReturn(Status::Code::S_400_BAD_REQUEST, "invalid_request", "Expected a JSON object or array");
    if (!Globals::fastRpc->checkConnectionKey(route.target))
        return APIReturn(Status::Code::S_503_SERVICE_UNAVAILABLE, "service_unavailable",
                         "Audit service is disconnected");
    boost::json::value rpcError;
    auto result =
        Globals::fastRpc->runRemoteRPCMethod(route.target, route.method, *request.inputJSON, &rpcError, false);
    if (!DataFormat::JSON::ASBOOL(rpcError, "succeed", false))
    {
        LOG_APP->log2(__func__, request.jwtToken->getSubject(), client.ipAddress, Runtime::Logs::ERROR,
                      "Backend call failed: %s", route.method);
        return APIReturn(Status::Code::S_502_BAD_GATEWAY, "backend_error",
                         "Audit service could not complete the request");
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
    const char *apiKeyEnvironment = std::getenv("UAUDIT_API_KEY");
    const char *appNameEnvironment = std::getenv("UAUDIT_APP");
    const std::string apiKey =
        apiKeyEnvironment ? apiKeyEnvironment : Globals::config.get<std::string>("Authentication.APIKey", "");
    const std::string appName = appNameEnvironment
                                    ? appNameEnvironment
                                    : Globals::config.get<std::string>("Authentication.AppName", "UAUDITANALYZER");
    if (apiKey.empty() || boost::starts_with(apiKey, "REPLACEME") || appName.empty())
    {
        LOG_APP->log0(__func__, Runtime::Logs::CRITICAL, "Configure UAUDIT_API_KEY for uFastAuthD3 APISync");
        return false;
    }
    std::map<std::string, std::string> templateVariables{{"APIKEY", apiKey}, {"APP", appName}};
    Globals::webServer.reset(ConfigBuilder::Server_WebRESTFul::Builder::createRESTfulEngine(
        *webConfig, LOG_APP, LOG_RPC, appName, "/var/www/uauditweb3",
        ConfigBuilder::Server_WebRESTFul::REST_ENGINE_MANDATORY_SSL, templateVariables));
    if (!Globals::webServer)
        return false;
    Client::APISync::APISyncParameters syncParameters;
    syncParameters.loadFromInfoTree(webConfig->get_child("APISync"));
    auto syncResult = Client::APISync::updateAccessControlContext(
        LOG_APP.get(), &syncParameters, appName, apiKey,
        boost::json::parse(
            R"([{"id":"stats","description":"Read audit statistics and configuration"},{"id":"control","description":"Modify audit rules, actions and inputs"}])"),
        boost::json::parse(
            R"([{"id":"AUDIT_READER","description":"Audit reader","scopes":["stats"]},{"id":"AUDIT_ADMIN","description":"Audit administrator","scopes":["stats","control"]}])"),
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
    engine->config.appName = appName;
    engine->config.setSoftwareVersion(atoi(PROJECT_VER_MAJOR), atoi(PROJECT_VER_MINOR), atoi(PROJECT_VER_PATCH),
                                      "stable");
    engine->endpointsHandler[1] = endpoints;
    engine->startInBackground();
    return true;
}

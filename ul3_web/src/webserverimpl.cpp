/**
 * @file
 * @brief Implementation for ul3_web/src/webserverimpl.
 */
#include "webserverimpl.h"
#include "globals.h"
#include <Mantids30/Client/APISync/apisync.h>
#include <Mantids30/ConfigBuilder/Server_WebRESTFul/config_builder.h>
#include <atomic>
#include <boost/algorithm/string/predicate.hpp>
#include <chrono>
#include <config.h>
#include <cstdlib>
using namespace Mantids30;
using namespace UL3::WEB;
using namespace Server::WebRESTFul;
using namespace Protocol::HTTP;
namespace {
/** @brief Endpoint-owned prefixes keep read and write RPC permissions separate. */
std::string statsScope = "stats";
std::string controlScope = "control";
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
 * @brief Forward a JSON call to the endpoint's stats or control RPC namespace.
 * @param context Endpoint-owned scope used as the RPC method prefix.
 * @param request Authenticated request containing the JSON payload and session token.
 * @param client Client address used in backend failure diagnostics.
 * @return Backend result, or an HTTP error for CSRF, payload, connection or RPC failures.
 * @note Debug traces correlate routing stages and elapsed time without recording payload contents.
 */
static APIReturn forward(void *context, const RESTful::RequestContext &request, Sessions::ClientDetails &client)
{
    const auto &scope = *static_cast<const std::string *>(context);
    // Correlate interleaved requests without logging session tokens or payload contents.
    static std::atomic<unsigned long long> nextRequestId{1};
    const auto requestId = nextRequestId.fetch_add(1, std::memory_order_relaxed);
    const auto started = std::chrono::steady_clock::now();
    LOG_APP->log2(__func__,
                  request.jwtToken->getSubject(),
                  client.ipAddress,
                  Runtime::Logs::DEBUG,
                  "RPC forward: requestId=%llu stage=%s scope=%s target=%s method=%s elapsedMs=%.3f",
                  requestId,
                  "received",
                  scope.c_str(),
                  "",
                  "",
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    // JWT authentication identifies the session; mutations also bind to its CSRF token.
    if (scope == "control" && (request.jwtToken->getJwtId().empty() || request.clientRequest->headers.getOptionValueStringByName("X-CSRF-Token") != request.jwtToken->getJwtId()))
    {
        LOG_APP->log2(__func__,
                      request.jwtToken->getSubject(),
                      client.ipAddress,
                      Runtime::Logs::DEBUG,
                      "RPC forward: requestId=%llu stage=%s scope=%s target=%s method=%s elapsedMs=%.3f",
                      requestId,
                      "invalid_csrf_token",
                      scope.c_str(),
                      "",
                      "",
                      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        return {Status::Code::S_403_FORBIDDEN, "invalid_csrf_token", "Refresh the audit session and try again"};
    }
    if (!request.inputJSON->is_object())
    {
        LOG_APP->log2(__func__,
                      request.jwtToken->getSubject(),
                      client.ipAddress,
                      Runtime::Logs::DEBUG,
                      "RPC forward: requestId=%llu stage=%s scope=%s target=%s method=%s elapsedMs=%.3f",
                      requestId,
                      "invalid_request_body",
                      scope.c_str(),
                      "",
                      "",
                      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        return {Status::Code::S_400_BAD_REQUEST, "invalid_request", "Expected target, method and optional payload"};
    }
    const std::string target(DataFormat::JSON::ASSTRING(*request.inputJSON, "target", ""));
    const std::string method(DataFormat::JSON::ASSTRING(*request.inputJSON, "method", ""));
    // A bare method name cannot override the namespace fixed by the endpoint's permission.
    if ((target != "WEB" && target != "ALERTS" && target != "LOG2JSON") || method.empty()
        || method.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
    {
        LOG_APP->log2(__func__,
                      request.jwtToken->getSubject(),
                      client.ipAddress,
                      Runtime::Logs::DEBUG,
                      "RPC forward: requestId=%llu stage=%s scope=%s target=%s method=%s elapsedMs=%.3f",
                      requestId,
                      "invalid_target_or_method",
                      scope.c_str(),
                      "",
                      "",
                      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        return {Status::Code::S_400_BAD_REQUEST, "invalid_request", "Expected an audit service and a bare RPC method name"};
    }
    const auto *payload = request.inputJSON->as_object().if_contains("payload");
    if (payload && !payload->is_null() && !payload->is_object() && !payload->is_array())
    {
        LOG_APP->log2(__func__,
                      request.jwtToken->getSubject(),
                      client.ipAddress,
                      Runtime::Logs::DEBUG,
                      "RPC forward: requestId=%llu stage=%s scope=%s target=%s method=%s elapsedMs=%.3f",
                      requestId,
                      "invalid_payload_type",
                      scope.c_str(),
                      target.c_str(),
                      method.c_str(),
                      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        return {Status::Code::S_400_BAD_REQUEST, "invalid_request", "Expected a JSON object or array payload"};
    }
    const auto rpcMethod = scope + "." + method;
    const auto &parameters = payload ? *payload : boost::json::value(boost::json::object{});
    boost::json::value result;
    // Keep transport diagnostics until the failure is logged, without exposing them to clients.
    boost::json::value rpcError;
    bool succeeded = false;
    if (target == "WEB")
    {
        // Dispatch through the same registry and namespace without a loopback connection.
        LOG_APP->log2(__func__,
                      request.jwtToken->getSubject(),
                      client.ipAddress,
                      Runtime::Logs::DEBUG,
                      "RPC forward: requestId=%llu stage=%s scope=%s target=%s method=%s elapsedMs=%.3f",
                      requestId,
                      "local_dispatch",
                      scope.c_str(),
                      target.c_str(),
                      rpcMethod.c_str(),
                      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        result = Globals::fastRpc->runLocalRPCMethod(rpcMethod, "WEB", "", nullptr, parameters, &succeeded);
    }
    else
    {
        LOG_APP->log2(__func__,
                      request.jwtToken->getSubject(),
                      client.ipAddress,
                      Runtime::Logs::DEBUG,
                      "RPC forward: requestId=%llu stage=%s scope=%s target=%s method=%s elapsedMs=%.3f",
                      requestId,
                      "checking_connection",
                      scope.c_str(),
                      target.c_str(),
                      rpcMethod.c_str(),
                      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        if (!Globals::fastRpc->checkConnectionKey(target))
        {
            LOG_APP->log2(__func__,
                          request.jwtToken->getSubject(),
                          client.ipAddress,
                          Runtime::Logs::DEBUG,
                          "RPC forward: requestId=%llu stage=%s scope=%s target=%s method=%s elapsedMs=%.3f",
                          requestId,
                          "service_disconnected",
                          scope.c_str(),
                          target.c_str(),
                          rpcMethod.c_str(),
                          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
            return {Status::Code::S_503_SERVICE_UNAVAILABLE, "service_unavailable", "Audit service is disconnected"};
        }
        LOG_APP->log2(__func__,
                      request.jwtToken->getSubject(),
                      client.ipAddress,
                      Runtime::Logs::DEBUG,
                      "RPC forward: requestId=%llu stage=%s scope=%s target=%s method=%s elapsedMs=%.3f",
                      requestId,
                      "remote_dispatch",
                      scope.c_str(),
                      target.c_str(),
                      rpcMethod.c_str(),
                      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        result = Globals::fastRpc->runRemoteRPCMethod(target, rpcMethod, parameters, &rpcError, false);
        succeeded = DataFormat::JSON::ASBOOL(rpcError, "succeed", false);
    }
    LOG_APP->log2(__func__,
                  request.jwtToken->getSubject(),
                  client.ipAddress,
                  Runtime::Logs::DEBUG,
                  "RPC forward: requestId=%llu stage=%s scope=%s target=%s method=%s elapsedMs=%.3f",
                  requestId,
                  succeeded ? "rpc_succeeded" : "rpc_failed",
                  scope.c_str(),
                  target.c_str(),
                  rpcMethod.c_str(),
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    if (!succeeded)
    {
        LOG_APP->log2(__func__,
                      request.jwtToken->getSubject(),
                      client.ipAddress,
                      Runtime::Logs::ERROR,
                      "Backend call failed: requestId=%llu target=%s method=%s error=%s",
                      requestId,
                      target.c_str(),
                      rpcMethod.c_str(),
                      boost::json::serialize(rpcError).c_str());
        return {Status::Code::S_502_BAD_GATEWAY, "backend_error", "Audit service could not complete the request"};
    }
    APIReturn response(result);
    response.httpExtraHeaders["Cache-Control"] = "no-store";
    LOG_APP->log2(__func__,
                  request.jwtToken->getSubject(),
                  client.ipAddress,
                  Runtime::Logs::DEBUG,
                  "RPC forward: requestId=%llu stage=%s scope=%s target=%s method=%s elapsedMs=%.3f",
                  requestId,
                  "response_ready",
                  scope.c_str(),
                  target.c_str(),
                  rpcMethod.c_str(),
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
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
    endpoints->addEndpoint(Method::POST, "remote.stats", Security::JWT_COOKIE_AUTH, {"stats"}, &statsScope, &forward);
    endpoints->addEndpoint(Method::POST, "remote.control", Security::JWT_COOKIE_AUTH, {"control"}, &controlScope, &forward);
    auto &engine = Globals::webServer;
    engine->config.appName = appName;
    engine->config.setSoftwareVersion(atoi(PROJECT_VER_MAJOR), atoi(PROJECT_VER_MINOR), atoi(PROJECT_VER_PATCH), "stable");
    engine->endpointsHandler[1] = endpoints;
    engine->startInBackground();
    return true;
}

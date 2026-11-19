/**
 * @file
 * @brief Declarations and contracts for ul3_alert/src/rpcimpl.
 */
#pragma once

#include <audit_json.h>

namespace UL3::JSONALERT {

/** @brief Register backend RPC operations and maintain the outbound TLS connection to the web service. */
class RpcImpl
{
public:
    /**
     * @brief Expose backend controls and statistics over the outbound FastRPC1 connection.
     */
    RpcImpl();
    /**
     * @brief Register backend RPC handlers and reconnect to the TLS web service indefinitely.
     * @note Workers use the existing service-lifetime shutdown model.
     */
    static void runRpcClient();

private:
    /**
     * @brief Persist and reload rule definitions supplied in the RPC request.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static bool controlWriteRulesFileConfig(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &);
    /**
     * @brief Return persisted rule definitions through RPC.
     */
    static boost::json::value statReadRulesFileConfig(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /**
     * @brief Return the running rule definitions through RPC.
     */
    static boost::json::array statReadRunningRules(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /**
     * @brief Return a running rule selected by the request id.
     */
    static boost::json::value statReadRunningRule(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /**
     * @brief Return whether the running rules have unsaved changes.
     */
    static bool statAreRulesModified(void *, const std::string &, const boost::json::value &, void *, const std::string &);

    /**
     * @brief Remove the rule selected by the request position.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static bool controlRemoveRule(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &);
    /**
     * @brief Remove the action selected by the request name.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static bool controlRemoveAction(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &);

    /**
     * @brief Persist the currently running rule definitions.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static bool controlSaveRunningRules(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &);
    /**
     * @brief Persist the currently running action definitions.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static bool controlSaveRunningActions(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &);

    /**
     * @brief Add the named action supplied in the RPC request.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static bool controlAddAction(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &);
    /**
     * @brief Replace the named action supplied in the RPC request.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static bool controlEditAction(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &);
    /**
     * @brief Replace the rule selected by the request position.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static bool controlEditRule(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &);
    /**
     * @brief Insert the supplied rule at the request position.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static bool controlAddRule(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &);

    /**
     * @brief Move the selected rule one position earlier in evaluation order.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static bool controlRuleUp(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &);
    /**
     * @brief Move the selected rule one position later in evaluation order.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static bool controlRuleDown(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &);

    /**
     * @brief Persist and reload the supplied action definitions.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static bool controlWriteActionsFileConfig(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &);
    /**
     * @brief Return whether the running actions have unsaved changes.
     */
    static bool statAreActionsModified(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /**
     * @brief Return persisted action definitions through RPC.
     */
    static boost::json::value statReadActionsFileConfig(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /**
     * @brief Return running action definitions through RPC.
     */
    static boost::json::array statReadRunningActions(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /**
     * @brief Return the running action selected by the request name.
     */
    static boost::json::value statReadRunningAction(void *, const std::string &, const boost::json::value &, void *, const std::string &);

    /**
     * @brief Remove the persisted input selected by the request id.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static bool controlRemoveInput(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &);
    /**
     * @brief Add a persisted input definition and assign its identifier.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static bool controlAddInput(void *, const std::string &, const boost::json::value &configValue, void *, const std::string &);
    /**
     * @brief Return persisted input definitions and their modification status.
     */
    static boost::json::object statReadInputsFileConfig(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /**
     * @brief Return statistics for all running input servers.
     */
    static boost::json::array statReadRunningInputs(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /**
     * @brief Schedule a delayed process exit so the service manager can restart the backend.
     */
    static bool controlReloadApp(void *, const std::string &, const boost::json::value &, void *, const std::string &);

    /**
     * @brief Return evaluation queue, throughput and rule activation statistics.
     */
    static boost::json::object statSystemStats(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /**
     * @brief Reload running rules from the configured file.
     */
    static bool controlRulesReload(void *, const std::string &, const boost::json::value &, void *, const std::string &);
    /**
     * @brief Reload running actions from the configured file.
     */
    static bool controlActionsReload(void *, const std::string &, const boost::json::value &, void *, const std::string &);
};

} // namespace UL3::JSONALERT

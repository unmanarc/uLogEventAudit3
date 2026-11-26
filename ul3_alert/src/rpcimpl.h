/**
 * @file
 * @brief Declarations and contracts for ul3_alert/src/rpcimpl.
 */
#pragma once

#include <audit_json.h>
#include <memory>

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
    static boost::json::value controlWriteRulesFileConfig(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Return persisted rule definitions through RPC.
     */
    static boost::json::value statReadRulesFileConfig(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Return the running rule definitions through RPC.
     */
    static boost::json::value statReadRunningRules(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Return a running rule selected by the request id.
     */
    static boost::json::value statReadRunningRule(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Return whether the running rules have unsaved changes.
     */
    static boost::json::value statAreRulesModified(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &);

    /**
     * @brief Remove the rule selected by the request position.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static boost::json::value controlRemoveRule(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Remove the action selected by the request name.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static boost::json::value controlRemoveAction(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &);

    /**
     * @brief Persist the currently running rule definitions.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static boost::json::value controlSaveRunningRules(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Persist the currently running action definitions.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static boost::json::value controlSaveRunningActions(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &);

    /**
     * @brief Add the named action supplied in the RPC request.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static boost::json::value controlAddAction(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Replace the named action supplied in the RPC request.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static boost::json::value controlEditAction(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Replace the rule selected by the request position.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static boost::json::value controlEditRule(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Insert the supplied rule at the request position.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static boost::json::value controlAddRule(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &);

    /**
     * @brief Move the selected rule one position earlier in evaluation order.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static boost::json::value controlRuleUp(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Move the selected rule one position later in evaluation order.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static boost::json::value controlRuleDown(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &);

    /**
     * @brief Persist and reload the supplied action definitions.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static boost::json::value controlWriteActionsFileConfig(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Return whether the running actions have unsaved changes.
     */
    static boost::json::value statAreActionsModified(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Return persisted action definitions through RPC.
     */
    static boost::json::value statReadActionsFileConfig(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Return running action definitions through RPC.
     */
    static boost::json::value statReadRunningActions(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Return the running action selected by the request name.
     */
    static boost::json::value statReadRunningAction(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &);

    /**
     * @brief Remove the persisted input selected by the request id.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static boost::json::value controlRemoveInput(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Add a persisted input definition and assign its identifier.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static boost::json::value controlAddInput(const std::shared_ptr<void> &, const std::string &, const boost::json::value &configValue, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Return persisted input definitions and their modification status.
     */
    static boost::json::value statReadInputsFileConfig(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Return statistics for all running input servers.
     */
    static boost::json::value statReadRunningInputs(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Schedule a delayed process exit so the service manager can restart the backend.
     */
    static boost::json::value controlReloadApp(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &);

    /**
     * @brief Return evaluation queue, throughput and rule activation statistics.
     */
    static boost::json::value statSystemStats(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Reload running rules from the configured file.
     */
    static boost::json::value controlRulesReload(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &);
    /**
     * @brief Reload running actions from the configured file.
     */
    static boost::json::value controlActionsReload(const std::shared_ptr<void> &, const std::string &, const boost::json::value &, const std::shared_ptr<void> &, const std::string &);
};

} // namespace UL3::JSONALERT

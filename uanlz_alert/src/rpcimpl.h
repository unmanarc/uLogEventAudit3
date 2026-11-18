/**
 * @file
 * @brief Declarations and contracts for uanlz_alert/src/rpcimpl.
 */
#ifndef RPCIMPL_H
#define RPCIMPL_H

#include <audit_json.h>

namespace UANLZ
{
namespace JSONALERT
{

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
    static json controlWriteRulesFileConfig(void *, const std::string &, const json &configValue, void *,
                                            const std::string &);
    /**
     * @brief Return persisted rule definitions through RPC.
     */
    static json statReadRulesFileConfig(void *, const std::string &, const json &, void *, const std::string &);
    /**
     * @brief Return the running rule definitions through RPC.
     */
    static json statReadRunningRules(void *, const std::string &, const json &, void *, const std::string &);
    /**
     * @brief Return a running rule selected by the request id.
     */
    static json statReadRunningRule(void *, const std::string &, const json &, void *, const std::string &);
    /**
     * @brief Return whether the running rules have unsaved changes.
     */
    static json statAreRulesModified(void *, const std::string &, const json &, void *, const std::string &);

    /**
     * @brief Remove the rule selected by the request position.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static json controlRemoveRule(void *, const std::string &, const json &configValue, void *, const std::string &);
    /**
     * @brief Remove the action selected by the request name.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static json controlRemoveAction(void *, const std::string &, const json &configValue, void *, const std::string &);

    /**
     * @brief Persist the currently running rule definitions.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static json controlSaveRunningRules(void *, const std::string &, const json &configValue, void *,
                                        const std::string &);
    /**
     * @brief Persist the currently running action definitions.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static json controlSaveRunningActions(void *, const std::string &, const json &configValue, void *,
                                          const std::string &);

    /**
     * @brief Add the named action supplied in the RPC request.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static json controlAddAction(void *, const std::string &, const json &configValue, void *, const std::string &);
    /**
     * @brief Replace the named action supplied in the RPC request.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static json controlEditAction(void *, const std::string &, const json &configValue, void *, const std::string &);
    /**
     * @brief Replace the rule selected by the request position.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static json controlEditRule(void *, const std::string &, const json &configValue, void *, const std::string &);
    /**
     * @brief Insert the supplied rule at the request position.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static json controlAddRule(void *, const std::string &, const json &configValue, void *, const std::string &);

    /**
     * @brief Move the selected rule one position earlier in evaluation order.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static json controlRuleUp(void *, const std::string &, const json &configValue, void *, const std::string &);
    /**
     * @brief Move the selected rule one position later in evaluation order.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static json controlRuleDown(void *, const std::string &, const json &configValue, void *, const std::string &);

    /**
     * @brief Persist and reload the supplied action definitions.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static json controlWriteActionsFileConfig(void *, const std::string &, const json &configValue, void *,
                                              const std::string &);
    /**
     * @brief Return whether the running actions have unsaved changes.
     */
    static json statAreActionsModified(void *, const std::string &, const json &, void *, const std::string &);
    /**
     * @brief Return persisted action definitions through RPC.
     */
    static json statReadActionsFileConfig(void *, const std::string &, const json &, void *, const std::string &);
    /**
     * @brief Return running action definitions through RPC.
     */
    static json statReadRunningActions(void *, const std::string &, const json &, void *, const std::string &);
    /**
     * @brief Return the running action selected by the request name.
     */
    static json statReadRunningAction(void *, const std::string &, const json &, void *, const std::string &);

    /**
     * @brief Remove the persisted input selected by the request id.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static json controlRemoveInput(void *, const std::string &, const json &configValue, void *, const std::string &);
    /**
     * @brief Add a persisted input definition and assign its identifier.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     */
    static json controlAddInput(void *, const std::string &, const json &configValue, void *, const std::string &);
    /**
     * @brief Return persisted input definitions and their modification status.
     */
    static json statReadInputsFileConfig(void *, const std::string &, const json &, void *, const std::string &);
    /**
     * @brief Return statistics for all running input servers.
     */
    static json statReadRunningInputs(void *, const std::string &, const json &, void *, const std::string &);
    /**
     * @brief Schedule a delayed process exit so the service manager can restart the backend.
     */
    static json controlReloadApp(void *, const std::string &, const json &, void *, const std::string &);

    /**
     * @brief Return evaluation queue, throughput and rule activation statistics.
     */
    static json statSystemStats(void *, const std::string &, const json &, void *, const std::string &);
    /**
     * @brief Reload running rules from the configured file.
     */
    static json controlRulesReload(void *, const std::string &, const json &, void *, const std::string &);
    /**
     * @brief Reload running actions from the configured file.
     */
    static json controlActionsReload(void *, const std::string &, const json &, void *, const std::string &);
};

} // namespace JSONALERT
} // namespace UANLZ

#endif // RPCIMPL_H

/**
 * @file
 * @brief Declarations and contracts for uanlz_alert/src/input/inputs.
 */
#ifndef INPUTS_H
#define INPUTS_H

#include <mutex>
#include <memory>
#include <list>
#include "tcpserver.h"

namespace UANLZ
{
namespace JSONALERT
{
namespace Input
{

/** @brief Own the configured TCP input servers for this service. */
class Inputs
{
public:
    /**
     * @brief Manage the configured TCP input servers.
     */
    Inputs();
    /**
     * @brief Return a JSON snapshot of counters and current resource usage.
     */
    static json getStats();
    /**
     * @brief Read input definitions, accepting a legacy null document as an empty list.
     * @param readOk Optional output flag set when the input document was read successfully.
     */
    static json readInputs(bool *readOk = nullptr);
    /**
     * @brief Persist an array of input definitions and mark changed configuration.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool writeInputs(const json &configValue);

    /**
     * @brief Assign an input identifier and prepend its definition to the persisted configuration.
     * @param configValue JSON configuration or RPC arguments expected by this operation.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool addInput(json configValue);
    /**
     * @brief Remove the matching input definition from persisted configuration.
     * @param inputId Identifier assigned when the input definition was created.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool removeInput(const std::string &inputId);

    /**
     * @brief Load and register the configured TCP input or output definitions.
     * @return True when the operation accepts the request; false if it cannot be applied.
     */
    static bool loadConfig();
    /**
     * @brief Start the workers for all configured inputs or outputs.
     */
    static void startThreads();

    /**
     * @brief Select the file used to read and write input definitions.
     * @param value Path of the input configuration file.
     */
    static void setConfigFilePath(const std::string &value);

    /**
     * @brief Report whether input configuration has changed since startup.
     * @return True after the persisted input definitions have changed.
     */
    static bool getModified();

private:
    static std::list<std::unique_ptr<TcpServer>> tcpServers;
    static std::mutex registryMutex;
    static std::string configFilePath;
    static bool modified;
};

} // namespace Input
} // namespace JSONALERT
} // namespace UANLZ

#endif // INPUTS_H

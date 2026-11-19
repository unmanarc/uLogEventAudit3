/**
 * @file
 * @brief Implementation for uanlz_alert/src/input/inputs.
 */
#include "inputs.h"
#include <fstream>
#include <Mantids30/Core/Helpers/random.h>
#include <inttypes.h>

#include "../globals.h"

using namespace UANLZ::JSONALERT::Input;
using namespace Mantids30::Runtime;

std::list<std::unique_ptr<TcpServer>> Inputs::tcpServers;
std::mutex Inputs::registryMutex;
std::string Inputs::configFilePath;
bool Inputs::modified = false;

Inputs::Inputs() {}

json Inputs::getStats()
{
    boost::json::array inputStats;

    std::lock_guard<std::mutex> lock(registryMutex);
    for (const auto &inputServer : tcpServers)
    {
        inputStats.emplace_back(inputServer->getStats());
    }

    return inputStats;
}

json Inputs::readInputs(bool *readOk)
{
    json configuredInputs;
    if (readOk)
        *readOk = false;

    if (access(configFilePath.c_str(), R_OK))
    {
        // Create empty JSON for unexistent file:
        boost::json::array r;

        LOG_APP->log0(__func__, Logs::INFO, "Inputs file '%s' not found, creating an empty file.",
                      configFilePath.c_str());

        std::ofstream configFile(configFilePath, std::ifstream::binary);
        if (!configFile.is_open())
            return false;
        configFile << r;
        configFile.close();
    }

    // Read JSON from file:
    std::ifstream configFile(configFilePath, std::ifstream::binary);

    if (!configFile.is_open())
    {
        if (readOk)
            *readOk = false;
        return configuredInputs;
    }

    if (!UANLZ::JSON::readConfigArray(configFile, configuredInputs))
        return {};

    if (readOk)
        *readOk = true;

    return configuredInputs;
}

bool Inputs::writeInputs(const json &configValue)
{
    if (!configValue.is_array())
        return false;
    if (readInputs() == configValue)
        return true;

    std::ofstream configFile(configFilePath, std::ifstream::binary);
    if (!configFile.is_open())
        return false;
    configFile << configValue;
    configFile.close();
    modified = true;
    return true;
}

bool Inputs::addInput(json configValue)
{
    bool readOk;
    json configuredInputs = readInputs(&readOk);
    boost::json::array updatedInputs;
    if (!readOk)
        return false;

    if (!configValue.is_object())
        return false;
    const std::string inputId = Mantids30::Core::Helpers::Random::createRandomString(8);
    boost::json::object inputConfig{{"id", inputId}};
    for (const auto &member : *configValue.if_object())
    {
        if (member.key() != "id")
            inputConfig.emplace(member.key(), member.value());
    }

    std::string listenAddress, description;
    uint16_t listenPort;

    listenAddress = std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "ListenAddr", ""));
    listenPort = Mantids30::DataFormat::JSON::ASUINT(configValue, "ListenPort", 0);
    description = std::string(Mantids30::DataFormat::JSON::ASSTRING(configValue, "Description", ""));

    LOG_APP->log0(__func__, Logs::INFO, "Adding JSON input '%s' to config file: Description: '%s' / @%s:%" PRIu16,
                  inputId.c_str(), description.c_str(),
                  listenAddress.c_str(), listenPort);

    updatedInputs.emplace_back(std::move(inputConfig));
    // Read array:
    for (u_int32_t inputIndex = 0; inputIndex < configuredInputs.as_array().size(); inputIndex++)
    {
        updatedInputs.emplace_back(configuredInputs.as_array()[inputIndex]);
    }

    return writeInputs(updatedInputs);
}

bool Inputs::removeInput(const std::string &inputId)
{
    bool readOk;
    json configuredInputs = readInputs(&readOk);
    boost::json::array updatedInputs;
    if (!readOk)
        return false;
    // Read array:
    for (u_int32_t inputIndex = 0; inputIndex < configuredInputs.as_array().size(); inputIndex++)
    {
        if (std::string(Mantids30::DataFormat::JSON::ASSTRING(configuredInputs.as_array()[inputIndex], "id", "")) !=
            inputId)
            updatedInputs.emplace_back(configuredInputs.as_array()[inputIndex]);
        else
        {
            std::string listenAddress, description;
            uint16_t listenPort;

            listenAddress = std::string(
                Mantids30::DataFormat::JSON::ASSTRING(configuredInputs.as_array()[inputIndex], "ListenAddr", ""));
            listenPort = Mantids30::DataFormat::JSON::ASUINT(configuredInputs.as_array()[inputIndex], "ListenPort", 0);
            description = std::string(
                Mantids30::DataFormat::JSON::ASSTRING(configuredInputs.as_array()[inputIndex], "Description", ""));

            LOG_APP->log0(__func__, Logs::INFO,
                          "Removing JSON input '%s' from config file: Description: '%s' / @%s:%" PRIu16,
                          Mantids30::DataFormat::JSON::ASCSTRING(configuredInputs.as_array()[inputIndex], "id", ""),
                          description.c_str(), listenAddress.c_str(), listenPort);
        }
    }

    return writeInputs(updatedInputs);
}

bool Inputs::loadConfig()
{
    LOG_APP->log0(__func__, Logs::INFO, "Loading inputs from file: %s", configFilePath.c_str());

    bool readOk;
    json configuredInputs = readInputs(&readOk);

    if (!readOk)
    {
        return false;
    }

    // Read array:
    for (u_int32_t inputIndex = 0; inputIndex < configuredInputs.as_array().size(); inputIndex++)
    {
        auto inputServer = std::make_unique<TcpServer>();
        if (!inputServer->loadConfig(configuredInputs.as_array()[inputIndex]))
            continue;
        std::lock_guard<std::mutex> lock(registryMutex);
        tcpServers.push_back(std::move(inputServer));
    }
    return true;
}

void Inputs::startThreads()
{
    std::lock_guard<std::mutex> lock(registryMutex);
    for (const auto &inputServer : tcpServers)
    {
        inputServer->startListener();
    }
}

void Inputs::setConfigFilePath(const std::string &value)
{
    configFilePath = value;
}

bool Inputs::getModified()
{
    return modified;
}

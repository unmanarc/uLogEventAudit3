/**
 * @file
 * @brief Implementation for ul3_log2json/src/output/outputs.
 */
#include "outputs.h"
#include <stdexcept>
#include "../output/output_base.h"

#include "../output/output_jsontcp.h"

using namespace UL3::LOG2JSON::Output;

std::list<std::unique_ptr<OutputBase>> Outputs::outputs;
std::mutex Outputs::registryMutex;

UL3::LOG2JSON::EndpointConfig Outputs::configuration{false};

Outputs::Outputs() = default;

boost::json::object Outputs::readConfig()
{
    return configuration.read();
}

void Outputs::writeConfig(const boost::json::array &definitions)
{
    configuration.replace(definitions);
}

boost::json::array Outputs::getStats()
{
    boost::json::array outputStats;
    std::scoped_lock lock(registryMutex);
    for (const auto &output : outputs)
    {
        outputStats.emplace_back(output->getStats());
    }
    return outputStats;
}

void Outputs::loadConfig(const std::string &file)
{
    const auto definitions = configuration.load(file);
    std::scoped_lock lock(registryMutex);
    for (const auto &definition : definitions)
    {
        auto endpoint = std::make_unique<JsonTcpOutput>();
        if (!endpoint->loadConfig(definition.as_object()))
        {
            throw std::runtime_error("Cannot initialize TCP endpoint");
        }
        outputs.push_back(std::move(endpoint));
    }
}

void Outputs::startThreads()
{
    std::scoped_lock lock(registryMutex);
    for (const auto &output : outputs)
    {
        output->startThread();
    }
}

void Outputs::dispatchEvent(const boost::json::object &eventJson, const std::tuple<time_t, uint32_t, uint64_t> &eventId)
{
    std::scoped_lock lock(registryMutex);
    for (const auto &output : outputs)
    {
        output->logAuditEvent(eventJson, eventId);
    }
}

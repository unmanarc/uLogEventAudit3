/**
 * @file
 * @brief Implementation for uanlz_log2json/src/output/outputs.
 */
#include "outputs.h"
#include "../output/output_base.h"

#include "../output/output_jsontcp.h"

using namespace UANLZ::LOG2JSON::Output;

std::list<std::unique_ptr<OutputBase>> Outputs::outputs;
std::mutex Outputs::registryMutex;

Outputs::Outputs() {}

json Outputs::getStats()
{
    boost::json::array outputStats;
    std::lock_guard<std::mutex> lock(registryMutex);
    for (const auto &output : outputs)
    {
        outputStats.emplace_back(output->getStats());
    }
    return outputStats;
}

void Outputs::loadConfig(const std::string &file)
{

    auto output = std::make_unique<JsonTcpOutput>();
    if (!output->loadConfig(file))
        return;

    std::lock_guard<std::mutex> lock(registryMutex);
    outputs.push_back(std::move(output));
}

void Outputs::startThreads()
{
    std::lock_guard<std::mutex> lock(registryMutex);
    for (const auto &output : outputs)
    {
        output->startThread();
    }
}

void Outputs::dispatchEvent(const json &eventJson, const std::tuple<time_t, uint32_t, uint64_t> &eventId)
{
    std::lock_guard<std::mutex> lock(registryMutex);
    for (const auto &output : outputs)
    {
        output->logAuditEvent(eventJson, eventId);
    }
}

#include "outputs.h"
#include "../output/output_base.h"

#include "../output/output_jsontcp.h"

using namespace UANLZ::LOG2JSON::Output;

std::list<std::unique_ptr<Output_Base>> Outputs::outputBases;
std::mutex Outputs::mt;

Outputs::Outputs()
{

}

json Outputs::getStats()
{
    boost::json::array v;
    std::lock_guard<std::mutex> lock(mt);
    for (const auto &ob : outputBases)
    {
        v.emplace_back(ob->getStats());
    }
    return v;
}

void Outputs::loadConfig(const std::string &file)
{
    // fill output here.
    auto out = std::make_unique<Output_JSONTCP>();
    if (!out->loadConfig(file)) return;

    std::lock_guard<std::mutex> lock(mt);
    outputBases.push_back(std::move(out));

}

void Outputs::startThreads()
{
    std::lock_guard<std::mutex> lock(mt);
    for (const auto &ob : outputBases)
    {
        ob->startThread();
    }
}

void Outputs::pushToOutputBases(const json &eventJSON, const std::tuple<time_t, uint32_t, uint64_t> &eventId)
{
    std::lock_guard<std::mutex> lock(mt);
    for (const auto &ob : outputBases)
    {
        ob->logAuditEvent(eventJSON,eventId);
    }
}

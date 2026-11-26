/** @file @brief ResourceMonitor sampling, serialization and internal RPC implementation. */
#include "resource_sensing.h"
#include <algorithm>
#include <stdexcept>

using Mantids30::Core::Helpers::ResourceMonitor;

namespace {
/** @brief Preserve optional OS counters instead of representing missing data as zero. */
template<typename T>
boost::json::value nullable(const std::optional<T> &value)
{
    if (value)
    {
        return *value;
    }
    return nullptr;
}

/** @brief Calculate rates only across valid, non-regressing counters and positive time. */
boost::json::value rate(const std::optional<uint64_t> &current, const std::optional<uint64_t> &previous, double elapsed)
{
    if (!current || !previous || *current < *previous || elapsed <= 0.0)
    {
        return nullptr;
    }
    return static_cast<double>(*current - *previous) / elapsed;
}
} // namespace

boost::json::object UL3::resourceSampleJSON(const ResourceMonitor::Snapshot &current, const ResourceMonitor::Snapshot *previous, int64_t timestamp)
{
    boost::json::object sample{{"timestamp", timestamp},
                               {"processId", current.processId},
                               {"logicalProcessorCount", current.logicalProcessorCount},
                               {"processCpuPercent", nullable(current.processCpuPercent)},
                               {"systemCpuPercent", nullable(current.systemCpuPercent)},
                               {"processCpuSeconds", nullable(current.processCpuSeconds)},
                               {"processUptimeSeconds", nullable(current.processUptimeSeconds)},
                               {"systemUptimeSeconds", nullable(current.systemUptimeSeconds)},
                               {"residentMemoryBytes", nullable(current.residentMemoryBytes)},
                               {"peakResidentMemoryBytes", nullable(current.peakResidentMemoryBytes)},
                               {"processMemoryPercent", nullable(current.processMemoryPercent)},
                               {"virtualMemoryBytes", nullable(current.virtualMemoryBytes)},
                               {"privateCommitBytes", nullable(current.privateCommitBytes)},
                               {"threadCount", nullable(current.threadCount)},
                               {"handleCount", nullable(current.handleCount)},
                               {"systemMemory", nullptr},
                               {"processIO", nullptr}};
    if (current.systemMemory)
    {
        sample["systemMemory"] = boost::json::object{{"totalBytes", current.systemMemory->totalBytes},
                                                     {"availableBytes", current.systemMemory->availableBytes},
                                                     {"usedPercent", current.systemMemory->usedPercent}};
    }
    if (current.processIO)
    {
        const auto &io = *current.processIO;
        const auto *old = previous && previous->processIO && previous->processId == current.processId ? &*previous->processIO : nullptr;
        const double elapsed = previous ? std::chrono::duration<double>(current.sampledAt - previous->sampledAt).count() : 0.0;
        sample["processIO"] = boost::json::object{{"readBytes", io.readBytes},
                                                  {"writeBytes", io.writeBytes},
                                                  {"storageReadBytes", nullable(io.storageReadBytes)},
                                                  {"storageWriteBytes", nullable(io.storageWriteBytes)},
                                                  {"readBytesPerSecond", rate(io.readBytes, old ? std::optional<uint64_t>(old->readBytes) : std::nullopt, elapsed)},
                                                  {"writeBytesPerSecond", rate(io.writeBytes, old ? std::optional<uint64_t>(old->writeBytes) : std::nullopt, elapsed)},
                                                  {"storageReadBytesPerSecond", rate(io.storageReadBytes, old ? old->storageReadBytes : std::nullopt, elapsed)},
                                                  {"storageWriteBytesPerSecond", rate(io.storageWriteBytes, old ? old->storageWriteBytes : std::nullopt, elapsed)}};
    }
    return sample;
}

UL3::ResourceSensing::~ResourceSensing()
{
    stop();
}

UL3::ResourceSensing &UL3::ResourceSensing::instance()
{
    static ResourceSensing sensor;
    return sensor;
}

void UL3::ResourceSensing::start(const boost::property_tree::ptree &config, const std::string &component)
{
    // Bound both memory usage and the size of an individual FastRPC response.
    const auto capacity = config.get<int64_t>("ResourceMonitor.SamplesPerSeries", 300);
    if (capacity < 1 || capacity > 3000)
    {
        throw std::invalid_argument("ResourceMonitor.SamplesPerSeries must be between 1 and 3000");
    }
    stop();
    {
        std::scoped_lock lock(m_mutex);
        m_capacity = static_cast<size_t>(capacity);
        m_component = component;
        for (auto &series : m_series)
        {
            series.monitor.reset();
            series.previous.reset();
            series.samples.clear();
            series.next = std::chrono::steady_clock::now();
        }
        m_stopping = false;
    }
    m_worker = std::thread(&ResourceSensing::run, this);
}

void UL3::ResourceSensing::stop()
{
    {
        std::scoped_lock lock(m_mutex);
        m_stopping = true;
    }
    m_wake.notify_all();
    if (m_worker.joinable())
    {
        m_worker.join();
    }
}

boost::json::object UL3::ResourceSensing::history(const std::string &resolution) const
{
    std::scoped_lock lock(m_mutex);
    const auto found = std::find_if(m_series.begin(), m_series.end(), [&](const auto &series) { return resolution == series.resolution; });
    if (found == m_series.end())
    {
        throw std::invalid_argument("Resolution must be 3s, 1m or 60m");
    }
    boost::json::array samples;
    samples.reserve(found->samples.size());
    for (const auto &sample : found->samples)
    {
        samples.emplace_back(sample);
    }
    return {{"component", m_component},
            {"resolution", resolution},
            {"intervalSeconds", found->interval.count()},
            {"capacity", m_capacity},
            {"running", !m_stopping},
            {"latest", samples.empty() ? boost::json::value(nullptr) : samples.back()},
            {"samples", std::move(samples)}};
}

boost::json::value UL3::ResourceSensing::rpcHistory(const std::shared_ptr<void> &, const std::string &, const boost::json::value &request, const std::shared_ptr<void> &, const std::string &)
{
    const auto *resolution = request.is_object() ? request.as_object().if_contains("resolution") : nullptr;
    if (!resolution || !resolution->is_string())
    {
        return boost::json::object{{"error", "invalid_resolution"}};
    }
    try
    {
        return instance().history(std::string(resolution->as_string()));
    }
    catch (const std::invalid_argument &)
    {
        return boost::json::object{{"error", "invalid_resolution"}};
    }
}

void UL3::ResourceSensing::run()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_stopping)
    {
        for (auto &series : m_series)
        {
            if (std::chrono::steady_clock::now() < series.next)
            {
                continue;
            }
            // Native reads and JSON construction do not block API readers.
            lock.unlock();
            const auto current = series.monitor.sample();
            const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            auto sample = resourceSampleJSON(current, series.previous ? &*series.previous : nullptr, timestamp);
            series.previous = current;
            // Skip missed deadlines instead of generating a burst of artificial samples.
            do
            {
                series.next += series.interval;
            }
            while (series.next <= std::chrono::steady_clock::now());
            lock.lock();
            series.samples.push_back(std::move(sample));
            if (series.samples.size() > m_capacity)
            {
                series.samples.pop_front();
            }
            if (m_stopping)
            {
                break;
            }
        }
        const auto next = std::min_element(m_series.begin(), m_series.end(), [](const auto &left, const auto &right) { return left.next < right.next; })->next;
        m_wake.wait_until(lock, next, [this] { return m_stopping; });
    }
}

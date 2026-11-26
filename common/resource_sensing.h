/** @file @brief Bounded, process-local OS resource sensing shared by the three services. */
#pragma once

#include <Mantids30/Core/Helpers/resource_monitor.h>
#include <Mantids30/Protocol/FastRPC1/fastrpc.h>
#include <array>
#include <boost/property_tree/ptree.hpp>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace UL3 {

/**
 * @brief Serialize all available OS measurements, preserving unavailable values as null.
 * @param current Current native snapshot.
 * @param previous Previous snapshot of the same resolution, or nullptr for the baseline.
 * @param timestamp Unix wall-clock milliseconds for display; rates use monotonic time.
 * @return Snapshot with cumulative I/O counters and interval-average byte rates.
 */
boost::json::object resourceSampleJSON(const Mantids30::Core::Helpers::ResourceMonitor::Snapshot &current, const Mantids30::Core::Helpers::ResourceMonitor::Snapshot *previous, int64_t timestamp);

/** @brief Own one interruptible sensor thread and three independently sampled histories. */
class ResourceSensing
{
public:
    /** @brief Construct an idle sensor; start() begins OS sampling. */
    ResourceSensing() = default;
    /** @brief Stop and join the sensor before releasing its histories. */
    ~ResourceSensing();
    /** @brief Return the sensor belonging to the calling executable. */
    static ResourceSensing &instance();
    /**
     * @brief Start fresh in-memory histories at 3 s, 1 min and 60 min resolutions.
     * @param config Service configuration; ResourceMonitor.SamplesPerSeries defaults to 300.
     * @param component Stable process label (web, alerts or log2json).
     * @throws std::invalid_argument For a retention outside 1..3000 samples.
     * @note Lifecycle calls must be serialized by the application; reads are thread-safe.
     */
    void start(const boost::property_tree::ptree &config, const std::string &component);
    /** @brief Interrupt the next sampling deadline and join the worker; safe to repeat. */
    void stop();
    /**
     * @brief Read one bounded history in chronological order without sampling the OS.
     * @param resolution One of 3s, 1m or 60m.
     * @return Component, interval, capacity, latest sample and historical samples.
     * @throws std::invalid_argument For an unsupported resolution.
     */
    boost::json::object history(const std::string &resolution) const;
    /** @brief Validate the requested resolution and return this process's resource history. */
    static boost::json::value rpcHistory(const std::shared_ptr<void> &, const std::string &, const boost::json::value &request, const std::shared_ptr<void> &, const std::string &);

private:
    /** @brief Keep independent CPU/I/O baselines and deadlines for each resolution. */
    struct Series
    {
        const char *resolution;
        std::chrono::seconds interval;
        Mantids30::Core::Helpers::ResourceMonitor monitor;
        std::optional<Mantids30::Core::Helpers::ResourceMonitor::Snapshot> previous;
        std::chrono::steady_clock::time_point next;
        std::deque<boost::json::object> samples;
    };
    /** @brief Sample due series and sleep until the earliest monotonic deadline or shutdown. */
    void run();

    std::array<Series, 3> m_series{{{"3s", std::chrono::seconds(3)}, {"1m", std::chrono::seconds(60)}, {"60m", std::chrono::seconds(3600)}}};
    mutable std::mutex m_mutex; ///< Protect histories, component metadata and stop state.
    std::condition_variable m_wake;
    std::thread m_worker;
    bool m_stopping = true;
    size_t m_capacity = 300;
    std::string m_component;
};
} // namespace UL3

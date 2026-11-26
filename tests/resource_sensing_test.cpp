/** @file @brief Verify resource serialization, interval deltas, retention and sensor lifecycle. */
#include "resource_sensing.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using Mantids30::Core::Helpers::ResourceMonitor;
using UL3::ResourceSensing;

/** @brief Fail checks in release builds as well as debug builds. */
static void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

/** @brief Verify null handling, integer fidelity and rates over actual monotonic elapsed time. */
static void checkSerialization()
{
    ResourceMonitor::Snapshot previous;
    previous.processId = 123;
    previous.sampledAt = std::chrono::steady_clock::now();
    previous.processIO = ResourceMonitor::ProcessIO{100, 200, 10, std::nullopt};
    auto current = previous;
    current.sampledAt += std::chrono::seconds(60);
    current.processIO = ResourceMonitor::ProcessIO{700, 1400, 130, 500};
    current.residentMemoryBytes = std::numeric_limits<uint64_t>::max();
    current.threadCount = 0;
    auto json = UL3::resourceSampleJSON(current, &previous, 1700000000123);
    require(json.at("timestamp").as_int64() == 1700000000123, "Wall-clock milliseconds lost");
    require(json.at("residentMemoryBytes").as_uint64() == std::numeric_limits<uint64_t>::max(), "64-bit memory truncated");
    require(json.at("threadCount").as_uint64() == 0, "Valid zero treated as missing");
    require(json.at("systemMemory").is_null() && json.at("processCpuPercent").is_null(), "Unavailable counters fabricated");
    auto io = json.at("processIO").as_object();
    require(io.at("readBytesPerSecond").as_double() == 10.0, "Read rate has wrong interval");
    require(io.at("writeBytesPerSecond").as_double() == 20.0, "Write rate has wrong interval");
    require(io.at("storageReadBytesPerSecond").as_double() == 2.0, "Storage rate incorrect");
    require(io.at("storageWriteBytesPerSecond").is_null(), "Rate fabricated across a missing counter");
    current.processIO->readBytes = 1;
    json = UL3::resourceSampleJSON(current, &previous, 1);
    require(json.at("processIO").as_object().at("readBytesPerSecond").is_null(), "Regressing counter underflowed");
    current.sampledAt = previous.sampledAt;
    json = UL3::resourceSampleJSON(current, &previous, 1);
    require(json.at("processIO").as_object().at("writeBytesPerSecond").is_null(), "Zero-duration rate accepted");
    current.sampledAt += std::chrono::seconds(60);
    current.processId = 456;
    json = UL3::resourceSampleJSON(current, &previous, 1);
    require(json.at("processIO").as_object().at("writeBytesPerSecond").is_null(), "Rate crossed process identity");
    json = UL3::resourceSampleJSON(current, nullptr, 1);
    require(json.at("processIO").as_object().at("writeBytesPerSecond").is_null(), "Baseline has a rate");
    current.processIO.reset();
    require(UL3::resourceSampleJSON(current, &previous, 1).at("processIO").is_null(), "Missing I/O fabricated");
}

/** @brief Wait for the real worker without assuming a particular scheduling delay. */
static boost::json::object waitForSample(ResourceSensing &sensor, int64_t after)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    do
    {
        auto history = sensor.history("3s");
        if (history.at("latest").is_object() && history.at("latest").as_object().at("timestamp").as_int64() > after)
        {
            return history;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Sensor failed to publish a sample");
}

/** @brief Check independent baselines, history eviction, validation and prompt joined shutdown. */
static void checkLifecycle()
{
    ResourceSensing sensor;
    boost::property_tree::ptree config;
    for (const auto capacity : {-1, 0, 3001})
    {
        config.put("ResourceMonitor.SamplesPerSeries", capacity);
        bool rejected = false;
        try
        {
            sensor.start(config, "test");
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        require(rejected, "Invalid retention accepted");
    }
    config.put("ResourceMonitor.SamplesPerSeries", 1);
    sensor.start(config, "test");
    const auto first = waitForSample(sensor, 0);
    const auto firstTimestamp = first.at("latest").as_object().at("timestamp").as_int64();
    const auto second = waitForSample(sensor, firstTimestamp);
    require(second.at("samples").as_array().size() == 1, "History exceeded retention");
    require(second.at("capacity").as_uint64() == 1 && second.at("component").as_string() == "test", "Metadata lost");
    require(second.at("latest") == second.at("samples").as_array().back(), "Latest sample does not match history");
    require(second.at("latest").as_object().at("processCpuPercent").is_double(), "CPU baseline never advanced");
    for (const auto *resolution : {"1m", "60m"})
    {
        const auto history = sensor.history(resolution);
        require(history.at("samples").as_array().size() == 1, "Coarse series sampled at the fast interval");
        require(history.at("latest").as_object().at("processCpuPercent").is_null(), "Coarse CPU reused fast baseline");
    }
    bool rejected = false;
    try
    {
        sensor.history("invalid");
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    require(rejected, "Unknown resolution accepted");
    const auto before = std::chrono::steady_clock::now();
    sensor.stop();
    sensor.stop();
    require(std::chrono::steady_clock::now() - before < std::chrono::seconds(1), "Shutdown waited for a sample deadline");
    require(!sensor.history("3s").at("running").as_bool(), "Stopped sensor still running");
    sensor.start({}, "restarted");
    const auto restarted = waitForSample(sensor, 0);
    require(restarted.at("capacity").as_uint64() == 300, "Default retention changed");
    require(restarted.at("samples").as_array().size() == 1, "Restart retained old history");
    require(restarted.at("latest").as_object().at("processCpuPercent").is_null(), "Restart retained CPU baseline");
    const auto method = ResourceSensing::rpcMethod();
    for (const auto &request : {boost::json::value(nullptr), boost::json::value(boost::json::object{}), boost::json::value(boost::json::object{{"resolution", "wrong"}})})
    {
        const auto result = method.method(nullptr, "SERVER", request, nullptr, "");
        require(result.as_object().at("error").as_string() == "invalid_resolution", "RPC accepted malformed resolution");
    }
}

/** @brief Run deterministic serialization checks followed by a short real worker lifecycle. */
int main()
{
    try
    {
        checkSerialization();
        checkLifecycle();
        std::cout << "PASS: resource deltas, independent resolutions, bounded histories and shutdown\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

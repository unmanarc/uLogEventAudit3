/** @file @brief Verify endpoint validation, pending changes and transactional persistence. */
#include "endpointconfig.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

using UL3::LOG2JSON::EndpointConfig;

/** @brief Fail with a diagnostic in every build type. */
static void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

/** @brief Read the complete persisted document for preservation checks. */
static std::string read(const std::filesystem::path &path)
{
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), {}};
}

/** @brief Require a rejected save to preserve the saved snapshot and file. */
static void reject(EndpointConfig &config, const boost::json::array &definitions, const std::filesystem::path &path)
{
    const auto before = config.read();
    const auto document = read(path);
    bool rejected = false;
    try
    {
        config.replace(definitions);
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    require(rejected, "Invalid endpoint configuration accepted");
    require(config.read() == before, "Failed replacement changed saved configuration");
    require(read(path) == document, "Failed replacement changed the file");
}

/** @brief Exercise both schemas, restart semantics, failed saves and malformed startup files. */
static void check(const std::filesystem::path &directory, bool inputs)
{
    const auto path = directory / (inputs ? "inputs.json" : "outputs.json");
    const auto key = inputs ? "inputs" : "outputs";
    const auto portKey = inputs ? "ListenPort" : "Port";
    std::ofstream(path) << "[]";
    EndpointConfig config(inputs);
    require(config.load(path.string()).empty(), "Empty array rejected");
    const boost::json::object endpoint{{"id", "first"}, {"Description", "Unicode λ \"quoted\"\nendpoint"},
        {inputs ? "ListenAddr" : "Host", "127.0.0.1"}, {portKey, 12345}};
    config.replace({endpoint});
    const auto saved = config.read().at(key).as_array();
    require(config.read().at("modified").as_bool(), "Saved change not pending");
    require(boost::json::parse(read(path)) == saved, "Saved file changed values");
    struct stat status{};
    require(stat(path.c_str(), &status) == 0 && (status.st_mode & 0777) == 0600, "Saved file permissions not private");
    config.load(path.string());
    require(!config.read().at("modified").as_bool(), "Restart did not clear pending changes");
    config.replace({});
    require(config.read().at("modified").as_bool(), "Last removal not pending");
    config.replace(saved);
    require(!config.read().at("modified").as_bool(), "Reverting configuration did not clear pending changes");

    reject(config, {42}, path);
    reject(config, {boost::json::object{}}, path);
    reject(config, {endpoint, endpoint}, path);
    for (const boost::json::value value : {boost::json::value(0), boost::json::value(-1),
        boost::json::value(65536), boost::json::value(true), boost::json::value(12.5), boost::json::value("9999")})
    {
        auto invalid = endpoint;
        invalid[portKey] = value;
        reject(config, {invalid}, path);
    }
    auto invalid = endpoint;
    invalid[inputs ? "ListenAddr" : "Host"] = "";
    reject(config, {invalid}, path);
    invalid = endpoint;
    invalid["id"] = "second";
    if (inputs)
    {
        reject(config, {endpoint, invalid}, path);
    }
    else
    {
        for (const auto *number : {"TimeoutSecs", "ReconnectSleepTimeInSecs", "MaxQueuedItems"})
        {
            invalid = endpoint;
            invalid[number] = 0;
            reject(config, {invalid}, path);
        }
        invalid = endpoint;
        invalid["TransmissionMode"] = 2;
        reject(config, {invalid}, path);
        invalid = endpoint;
        invalid["QueuePushTimeoutInMS"] = uint64_t{1} << 32;
        reject(config, {invalid}, path);
    }

    // A rename failure must clean up the temporary file and retain the snapshot.
    std::filesystem::remove(path);
    std::filesystem::create_directory(path);
    const auto before = config.read();
    bool failed = false;
    try
    {
        config.replace({});
    }
    catch (const std::exception &)
    {
        failed = true;
    }
    require(failed && config.read() == before, "Persistence failure changed configuration");
    for (const auto &entry : std::filesystem::directory_iterator(directory))
    {
        require(entry.path().filename().string().find(".json.") == std::string::npos, "Temporary file leaked");
    }
    std::filesystem::remove(path);
    for (const auto *document : {"null", "{}", "[", "[42]"})
    {
        std::ofstream(path) << document;
        failed = false;
        try
        {
            config.load(path.string());
        }
        catch (const std::exception &)
        {
            failed = true;
        }
        require(failed && config.read() == before, "Bad startup file changed configuration");
    }
}

/** @brief Run in an isolated directory and remove all test artifacts. */
int main()
{
    std::string pattern = "/tmp/ul3-endpoints-XXXXXX";
    const auto *created = mkdtemp(pattern.data());
    if (!created)
    {
        return 1;
    }
    const std::filesystem::path directory(created);
    try
    {
        check(directory, true);
        check(directory, false);
        std::filesystem::remove_all(directory);
        std::cout << "PASS: endpoint validation, persistence and restart snapshots\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::filesystem::remove_all(directory);
        std::cerr << error.what() << '\n';
        return 1;
    }
}

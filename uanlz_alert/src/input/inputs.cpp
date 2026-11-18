#include "inputs.h"
#include <fstream>
#include <Mantids30/Core/Helpers/random.h>
#include <inttypes.h>

#include "../globals.h"

using namespace UANLZ::JSONALERT::Input;
using namespace Mantids30::Runtime;

std::list<std::unique_ptr<TCPServer>> Inputs::tcpServers;
std::mutex Inputs::mt;
std::string Inputs::sConfigFilePath;
bool Inputs::modified = false;

Inputs::Inputs()
{

}

json Inputs::getStats()
{
    boost::json::array v;

    std::lock_guard<std::mutex> lock(mt);
    for (const auto &i : tcpServers)
    {
        v.emplace_back(i->getStats());
    }

    return v;
}

json Inputs::readInputs(bool * readOK)
{
    json root;
    if (readOK) *readOK = false;

    if (access(sConfigFilePath.c_str(),R_OK))
    {
        // Create empty JSON for unexistent file:
        boost::json::array r;

        LOG_APP->log0(__func__,Logs::INFO,"Inputs file '%s' not found, creating an empty file.", sConfigFilePath.c_str());

        std::ofstream config_doc(sConfigFilePath, std::ifstream::binary);
        if (!config_doc.is_open()) return false;
        config_doc << r;
        config_doc.close();
    }

    // Read JSON from file:
    std::ifstream config_doc(sConfigFilePath, std::ifstream::binary);

    if (!config_doc.is_open())
    {
        if ( readOK) *readOK = false;
        return root;
    }

    if (!UANLZ::JSON::readConfigArray(config_doc, root)) return {};

    if ( readOK) *readOK = true;

    return root;
}

bool Inputs::writeInputs(const json &jConfig)
{
    if (!jConfig.is_array()) return false;
    if (readInputs() == jConfig)
        return true;

    std::ofstream config_doc(sConfigFilePath, std::ifstream::binary);
    if (!config_doc.is_open()) return false;
    config_doc << jConfig;
    config_doc.close();
    modified = true;
    return true;
}

bool Inputs::addInput(json jConfig)
{
    bool readOK;
    json root = readInputs(&readOK);
    boost::json::array rt;
    if (!readOK)
        return false;

    if (!jConfig.is_object()) return false;
    jConfig.as_object()["id"] = Mantids30::Core::Helpers::Random::createRandomString(8).c_str();

    std::string listenAddr, description;
    uint16_t listenPort;

    listenAddr = std::string(Mantids30::DataFormat::JSON::ASSTRING(jConfig,"ListenAddr",""));
    listenPort = UANLZ::JSON::asUInt32(jConfig,"ListenPort",0);
    description = std::string(Mantids30::DataFormat::JSON::ASSTRING(jConfig,"Description",""));

    LOG_APP->log0(__func__,Logs::INFO,"Adding JSON input '%s' to config file: Description: '%s' / @%s:%" PRIu16, Mantids30::DataFormat::JSON::ASCSTRING(jConfig,"id",""),description.c_str(),listenAddr.c_str(),listenPort );

    rt.emplace_back(jConfig);
    // Read array:
    for ( u_int32_t i=0 ; i< root.as_array().size(); i++ )
    {
        rt.emplace_back(root.as_array()[i]);
    }

    return writeInputs(rt);
}

bool Inputs::remInput(const std::string &inputId)
{
    bool readOK;
    json root = readInputs(&readOK);
    boost::json::array rt;
    if (!readOK)
        return false;
    // Read array:
    for ( u_int32_t i=0 ; i< root.as_array().size(); i++ )
    {
        if ( std::string(Mantids30::DataFormat::JSON::ASSTRING(root.as_array()[i],"id","")) != inputId )
            rt.emplace_back(root.as_array()[i]);
        else
        {
            std::string listenAddr, description;
            uint16_t listenPort;

            listenAddr = std::string(Mantids30::DataFormat::JSON::ASSTRING(root.as_array()[i],"ListenAddr",""));
            listenPort = UANLZ::JSON::asUInt32(root.as_array()[i],"ListenPort",0);
            description = std::string(Mantids30::DataFormat::JSON::ASSTRING(root.as_array()[i],"Description",""));

            LOG_APP->log0(__func__,Logs::INFO,"Removing JSON input '%s' from config file: Description: '%s' / @%s:%" PRIu16, Mantids30::DataFormat::JSON::ASCSTRING(root.as_array()[i],"id",""),description.c_str(),listenAddr.c_str(),listenPort );

        }
    }

    return writeInputs(rt);
}

bool Inputs::loadConfig()
{
    LOG_APP->log0(__func__,Logs::INFO,"Loading inputs from file: %s", sConfigFilePath.c_str());

    bool readOK;
    json root = readInputs(&readOK);

    if (!readOK)
    {
        return false;
    }

    // Read array:
    for ( u_int32_t i=0 ; i< root.as_array().size(); i++ )
    {
        auto in = std::make_unique<TCPServer>();
        if (!in->loadConfig(root.as_array()[i])) continue;
        std::lock_guard<std::mutex> lock(mt);
        tcpServers.push_back(std::move(in));
    }
    return true;
}

void Inputs::startThreads()
{
    std::lock_guard<std::mutex> lock(mt);
    for (const auto &i : tcpServers)
    {
        i->startThreaded();
    }
}

void Inputs::setConfigFilePath(const std::string &value)
{
    sConfigFilePath = value;
}

bool Inputs::getModified()
{
    return modified;
}

#include <Mantids30/Core/Threads/safe_queue.h>
#include <memory>
#include "events/audit_event.h"
#include "events/audit_var.h"
#include "globals.h"
#include "vars/namedefs.h"

#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace UANLZ::LOG2JSON;
using namespace UANLZ::LOG2JSON::AuditdEvents;

namespace {
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Tracked
{
    explicit Tracked(int &alive) : alive(alive) { ++alive; }
    ~Tracked() { --alive; }
    int &alive;
};

void queueOwnership()
{
    int alive = 0;
    {
        Mantids30::Core::Threads::Safe::Queue<Tracked> queue;
        queue.setMaxItems(1);
        auto first = std::make_unique<Tracked>(alive);
        require(queue.push(first.get(), 0), "first push");
        first.release();
        {
            auto rejected = std::make_unique<Tracked>(alive);
            require(!queue.push(rejected.get(), 0), "full queue must reject");
        }
        require(alive == 1, "rejected item must be destroyed");
        auto item = std::unique_ptr<Tracked>(queue.pop(0));
        require(item && queue.size() == 0 && alive == 1, "pop transfers ownership");
        require(!queue.pop(0), "empty queue returns no item");
        item.reset();
        require(alive == 0, "popped item destroyed by owner");
        auto pending = std::make_unique<Tracked>(alive);
        require(queue.push(pending.get(), 0), "pending item");
        pending.release();
    }
    require(alive == 0, "Mantids30 queue destroys pending items");
}

void auditVariables()
{
    Audit_Var head, tail;
    require(head.parse("EXECVE", "a0", "41420021"), "hex decoding");
    require(head.getRawPureSize() == 4 && head.getFancy() == "AB  ", "binary and display values");
    require(head.getRawPure()[2] == '\0', "raw embedded NUL preserved");
    auto copy = head;
    require(tail.parse("EXECVE", "a0", "4344"), "second chunk");
    require(head.append(tail) && head.append(tail), "multiple appends");
    require(head.getRawPureSize() == 8 && head.getFancy() == "AB  CDCD", "append updates length");
    require(head.getRawPure()[8] == '\0', "appended raw value terminated");
    require(copy.getRawPureSize() == 4 && copy.getFancy() == "AB  ", "independent value copy");
    require(head.parse("CWD", "cwd", "\"/tmp\""), "repeated parse");
    require(head.getFancy() == "/tmp", "repeated parse replaces data");
    require(!head.parse("EXECVE", "a0", "ABC"), "odd hex rejected");
    require(head.getRawPureSize() == 0 && head.getFancy().empty(), "invalid parse clears old data");
}

void eventReconstruction()
{
    Audit_Event event;
    event.setEventId({1700000000, 123, 9007199254740993ULL});
    event.setHostId({"test", "127.0.0.1"});
    event.insertClassContents("EXECVE", "argc=2 a0[0]=6563 a0[1]=686F");
    event.insertClassContents("EXECVE", "a1=68656C6C6F");
    event.insertClassContents("PATH", "item=0 name=\"/bin/echo\"");
    event.insertClassContents("PATH", "item=1 name=\"/tmp\"");
    require(event.process(), "process event");
    auto value = event.getJSON();
    const auto &object = value.as_object();
    const auto &args = object.at("EXECVE").as_array().at(0).as_object().at("argv").as_array();
    require(args.size() == 2 && args[0].as_string() == "echo" && args[1].as_string() == "hello",
            "split arguments and merged records reconstructed");
    require(object.at("PATH").as_array().size() == 2, "PATH records remain separate");
    require(object.at("INFO").as_object().at("id").as_uint64() == 9007199254740993ULL,
            "64-bit event ID preserved");
    require(event.getClassVars("EXECVE") != nullptr && event.getClassVars("missing") == nullptr,
            "borrowed class lookup");
    require(event.getMultiLineClassVars("PATH").size() == 2, "borrowed multi-class lookup");
    require(event.process() && event.getJSON() == value, "reprocessing does not append old chunks");
}

void socketAddresses()
{
    Audit_ClassType record;
    record.setClassTypeName("SOCKADDR");
    record.setRawInput("saddr=020000507F0000010000000000000000");
    require(record.processToVarContent(), "IPv4 address record");
    auto value = record.getJSON();
    require(value.as_object().at("ipaddr").as_string() == "127.0.0.1" &&
            value.as_object().at("port").as_string() == "80", "IPv4 decoding");
    for (const auto *input : {"saddr=0", "saddr=0200", "saddr=0200005", "saddr=GGGG"})
    {
        record.setRawInput(input);
        require(record.processToVarContent(), "malformed address record");
        require(!record.getJSON().as_object().contains("ipaddr"), "invalid address not decoded");
    }
    record.setRawInput("saddr=01002F746D702F736F636B00");
    record.processToVarContent();
    require(record.getJSON().as_object().at("unix_path").as_string() == "/tmp/sock", "UNIX path decoding");
}
}

int main()
{
    try
    {
        Globals::setAppLog(std::make_unique<Mantids30::Runtime::Logs::AppLog>(
            Mantids30::Runtime::Logs::Mode::STANDARD));
        NameDefs::init();
        queueOwnership();
        auditVariables();
        eventReconstruction();
        socketAddresses();
        std::cout << "PASS: queue ownership, binary variables, split events and socket addresses\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

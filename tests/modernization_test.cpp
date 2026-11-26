#include "events/auditd_event.h"
#include "events/auditd_field.h"
#include "globals.h"
#include "vars/namedefs.h"
#include <Mantids30/Core/Threads/safe_queue.h>
#include <memory>

#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace UL3::LOG2JSON;
using namespace UL3::LOG2JSON::AuditdEvents;

namespace {
/** @brief Fail the regression test with a diagnostic when an invariant is false. */
void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

struct Tracked
{
    /** @brief Count one live object for queue ownership assertions. */
    explicit Tracked(int &alive)
        : alive(alive)
    {
        ++alive;
    }
    /** @brief Count destruction when ownership leaves the queue or consumer. */
    ~Tracked() { --alive; }
    int &alive;
};

/** @brief Check the Mantids30 queue ownership contract on rejection, pop and destruction. */
void queueOwnership()
{
    int alive = 0;
    {
        Mantids30::Core::Threads::Safe::Queue<Tracked> queue;
        queue.setMaxItems(1);
        require(!queue.push(nullptr, 0), "null item must be rejected");
        auto first = std::make_shared<Tracked>(alive);
        require(queue.push(first, 0), "first push");
        std::weak_ptr<Tracked> firstObserver = first;
        first.reset();
        require(!firstObserver.expired(), "queue retains shared ownership");
        {
            auto rejected = std::make_shared<Tracked>(alive);
            require(!queue.push(rejected, 0), "full queue must reject");
            require(rejected.use_count() == 1, "rejection retains caller ownership only");
        }
        require(alive == 1, "rejected item must be destroyed");
        auto item = queue.pop(0);
        require(item && queue.size() == 0 && alive == 1, "pop transfers ownership");
        require(!queue.pop(0), "empty queue returns no item");
        item.reset();
        require(alive == 0, "popped item destroyed by owner");
        require(firstObserver.expired(), "last consumer releases popped item");
        auto pending = std::make_shared<Tracked>(alive);
        require(queue.push(pending, 0), "pending item");
        pending.reset();
    }
    require(alive == 0, "Mantids30 queue destroys pending items");
}

/** @brief Check binary decoding, repeated parsing and safe fragment concatenation. */
void auditVariables()
{
    AuditdField head, tail;
    require(head.parse("EXECVE", "a0", "41420021"), "hex decoding");
    require(head.getRawValueSize() == 4 && head.getDisplayValue() == "AB  ", "binary and display values");
    require(head.getRawValue()[2] == '\0', "raw embedded NUL preserved");
    auto copy = head;
    require(tail.parse("EXECVE", "a0", "4344"), "second chunk");
    require(head.append(tail) && head.append(tail), "multiple appends");
    require(head.getRawValueSize() == 8 && head.getDisplayValue() == "AB  CDCD", "append updates length");
    require(head.getRawValue()[8] == '\0', "appended raw value terminated");
    require(copy.getRawValueSize() == 4 && copy.getDisplayValue() == "AB  ", "independent value copy");
    require(head.parse("CWD", "cwd", "\"/tmp\""), "repeated parse");
    require(head.getDisplayValue() == "/tmp", "repeated parse replaces data");
    require(head.parse("EXECVE", "a0[12]", "6563686F") && head.getDisplayValue() == "echo", "indexed EXECVE argument decoded");
    require(head.parse("EXECVE", "a0_len", "42") && head.getDisplayValue() == "42", "EXECVE length metadata stays numeric text");
    for (const auto *quoted : {"'text'", "\"text\"", "(text)", "{text}"})
    {
        require(head.parse("CWD", "cwd", quoted) && head.getDisplayValue() == "text", "matching wrappers removed");
    }
    require(!head.parse("EXECVE", "a0", std::string("41\0G", 4)), "embedded NUL is not hexadecimal");
    require(!head.parse("EXECVE", "a0", "ABC"), "odd hex rejected");
    require(head.getRawValueSize() == 0 && head.getDisplayValue().empty(), "invalid parse clears old data");
}

/** @brief Check multi-record reconstruction and preservation of 64-bit event identities. */
void eventReconstruction()
{
    AuditdEvent event;
    event.setEventId({1700000000, 123, 9007199254740993ULL});
    event.setHostId({"test", "127.0.0.1"});
    event.insertRecord("EXECVE", "argc=2 a0[0]=6563 a0[1]=686F");
    event.insertRecord("EXECVE", "a1=68656C6C6F");
    event.insertRecord("PATH", "item=0 name=\"/bin/echo\"");
    event.insertRecord("PATH", "item=1 name=\"/tmp\"");
    require(event.process(), "process event");
    auto value = event.getJson();
    const auto &object = value;
    const auto &args = object.at("EXECVE").as_array().at(0).if_object()->at("argv").as_array();
    require(args.size() == 2 && args[0].as_string() == "echo" && args[1].as_string() == "hello", "split arguments and merged records reconstructed");
    require(object.at("PATH").as_array().size() == 2, "PATH records remain separate");
    require(object.at("INFO").if_object()->at("id").as_uint64() == 9007199254740993ULL, "64-bit event ID preserved");
    require(event.getFirstRecord("EXECVE") != nullptr && event.getFirstRecord("missing") == nullptr, "borrowed record lookup");
    require(event.getRecords("PATH").size() == 2, "borrowed multi-record lookup");
    require(event.process() && event.getJson() == value, "reprocessing does not append old chunks");
    require(!Mantids30::DataFormat::JSON::HAS_MEMBER(object.at("INFO"), "key"), "absent event key omitted");
    event.insertRecord("SYSCALL", "key=\"test-key\"");
    require(event.process(), "process keyed event");
    const auto keyedEvent = event.getJson();
    require(Mantids30::DataFormat::JSON::ASSTRING(Mantids30::DataFormat::JSON::MEMBER_OR_NULL(keyedEvent, "INFO"), "key", "") == "test-key", "record key propagated to event info");
}

/** @brief Quoted fields must not introduce fake fields or hide empty values. */
void quotedFields()
{
    AuditdRecord record;
    record.setRecordType("CWD");
    record.setRawInput(R"(cwd="/tmp fake=123" empty="" literal=7)");
    require(record.parseVariables(), "quoted record");
    const auto value = record.getJson();
    require(value.at("cwd").as_string() == "/tmp fake=123", "quoted whitespace and equals preserved");
    require(!value.contains("fake"), "quoted assignment is not a field");
    require(value.at("empty").as_string().empty(), "empty quoted value");
    require(value.at("literal").as_string() == "7", "unquoted value after quoted fields");
}

/** @brief Check valid and truncated socket-address records without unaligned accesses. */
void socketAddresses()
{
    AuditdRecord record;
    record.setRecordType("SOCKADDR");
    record.setRawInput("saddr=020000507F0000010000000000000000");
    require(record.parseVariables(), "IPv4 address record");
    auto value = record.getJson();
    require(value.at("ipaddr").as_string() == "127.0.0.1" && value.at("port").as_string() == "80", "IPv4 decoding");
    for (const auto *input : {"saddr=0", "saddr=0200", "saddr=0200005", "saddr=GGGG"})
    {
        record.setRawInput(input);
        require(record.parseVariables(), "malformed address record");
        require(!record.getJson().contains("ipaddr"), "invalid address not decoded");
    }
    record.setRawInput("saddr=01002F746D702F736F636B00");
    record.parseVariables();
    require(record.getJson().at("unix_path").as_string() == "/tmp/sock", "UNIX path decoding");
}
} // namespace

/** @brief Run the ownership and parser regression scenarios. */
int main()
{
    try
    {
        Globals::setAppLog(std::make_unique<Mantids30::Runtime::Logs::AppLog>(Mantids30::Runtime::Logs::Mode::STANDARD));
        NameDefs::init();
        queueOwnership();
        auditVariables();
        eventReconstruction();
        socketAddresses();
        quotedFields();
        std::cout << "PASS: queue ownership, binary variables, split events and socket addresses\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

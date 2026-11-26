/** @file @brief Action validation, safe event rendering and delivery. */
#include "action.h"

#include <Mantids30/Core/Memory/b_chunks.h>
#include <Mantids30/Core/Memory/streamable_string.h>
#include <Mantids30/Scripting/MantidsLang/mantidslang.h>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <memory>
#include <set>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
using namespace UL3::JSONALERT::Filters;
using namespace Mantids30::Core::Memory;

namespace {
constexpr size_t maxTemplateSize = 64 * 1024;
constexpr size_t maxRenderedSize = 1024 * 1024;

/** @brief Read a string field and reject missing, oversized or NUL-containing values. */
std::string text(const boost::json::object &object, const char *key, bool required = false)
{
    const auto *value = object.if_contains(key);
    if (!value)
    {
        if (required)
        {
            throw std::invalid_argument("Missing action field");
        }
        return {};
    }
    if (!value->is_string() || value->as_string().size() > maxTemplateSize ||
        value->as_string().find('\0') != boost::json::string::npos || (required && value->as_string().empty()))
    {
        throw std::invalid_argument("Invalid action field");
    }
    return std::string(value->as_string());
}

/** @brief Restrict templates to data operations, excluding local file rendering/includes. */
void validateTemplate(const std::string &source)
{
    static const boost::regex tags(R"(\{\{([^}]*?)\}\})");
    static const std::set<std::string> allowed{"print_raw", "print_json", "print_htmlescaped", "if", "foreach"};
    for (boost::sregex_iterator tag(source.begin(), source.end(), tags), end; tag != end; ++tag)
    {
        auto command = boost::trim_copy((*tag)[1].str());
        if (!command.empty() && command.front() == '#')
        {
            command = boost::trim_copy(command.substr(1));
            command = command.substr(0, command.find_first_of("[! \t\r\n"));
            if (allowed.count(command) == 0)
            {
                throw std::invalid_argument("Unsupported action template command");
            }
        }
    }
}

/** @brief Bound rendered output even when nested loops expand a small template. */
class RenderedText : public Streams::StreamableString
{
public:
    /** @brief Reject writes that would exceed the output budget. */
    std::optional<size_t> writeImpl(const void *buffer, const size_t &count) override
    {
        if (count > maxRenderedSize - getValue().size())
        {
            writeStatus.succeed = false;
            return std::nullopt;
        }
        return Streams::StreamableString::writeImpl(buffer, count);
    }
};
} // namespace

Action Action::fromConfig(const boost::json::value &config, const Action *previous)
{
    if (!config.is_object())
    {
        throw std::invalid_argument("Expected an action object");
    }
    auto object = config.as_object();
    text(object, "name", true);
    text(object, "description");
    const auto type = object.if_contains("type") ? text(object, "type", true) : "command";
    object["type"] = type;
    // Presence flags are response metadata. Only omission preserves a stored credential.
    object.erase("hasBotToken");
    object.erase("hasBearerToken");
    for (const auto *key : {"botToken", "bearerToken"})
    {
        if (!object.if_contains(key) && previous && text(previous->originalConfig.as_object(), "type") == type)
        {
            const auto *secret = previous->originalConfig.as_object().if_contains(key);
            if (secret)
            {
                object[key] = *secret;
            }
        }
    }
    if (type == "command")
    {
        text(object, "path", true);
        const auto *args = object.if_contains("args");
        if (args && (!args->is_array() || args->as_array().size() > 256))
        {
            throw std::invalid_argument("Invalid command arguments");
        }
        if (args)
        {
            for (const auto &argument : args->as_array())
            {
                text(boost::json::object{{"argument", argument}}, "argument");
            }
        }
        if (const auto *newline = object.if_contains("appendNewLineToEachArgv"); newline && !newline->is_bool())
        {
            throw std::invalid_argument("Invalid argument newline flag");
        }
        if (!object.if_contains("stdinTemplate"))
        {
            object["stdinTemplate"] = "{{#print_json[$]!}}\n";
        }
        validateTemplate(text(object, "stdinTemplate"));
    }
    else if (type == "http_post" || type == "telegram")
    {
        const auto *timeout = object.if_contains("timeoutSeconds");
        if (!timeout)
        {
            object["timeoutSeconds"] = 15;
            timeout = object.if_contains("timeoutSeconds");
        }
        if ((!timeout->is_int64() && !timeout->is_uint64()) ||
            (timeout->is_int64() && timeout->as_int64() <= 0) ||
            (timeout->is_uint64() && timeout->as_uint64() == 0) ||
            (timeout->is_uint64() ? timeout->as_uint64() : static_cast<uint64_t>(timeout->as_int64())) > 300)
        {
            throw std::invalid_argument("Invalid action timeout");
        }
        if (type == "http_post")
        {
            // Keep destinations literal. Event data cannot select a host or leak credentials via redirects.
            const auto url = text(object, "url", true);
            static const boost::regex urlPattern(R"(^https?://(\[[0-9A-Fa-f:.]+\]|[A-Za-z0-9.-]+)(:[0-9]{1,5})?([/?][^\s#]*)?$)");
            boost::smatch parsedURL;
            if (!boost::regex_match(url, parsedURL, urlPattern) ||
                (parsedURL[2].matched && (std::stoul(parsedURL[2].str().substr(1)) == 0 ||
                                         std::stoul(parsedURL[2].str().substr(1)) > 65535)))
            {
                throw std::invalid_argument("Invalid HTTP action URL");
            }
            if (!object.if_contains("contentType"))
            {
                object["contentType"] = "application/json";
            }
            const auto contentType = text(object, "contentType", true);
            const auto bearer = text(object, "bearerToken");
            if (contentType.find_first_of("\r\n") != std::string::npos || bearer.find_first_of("\r\n") != std::string::npos ||
                (!bearer.empty() && !boost::starts_with(url, "https://")))
            {
                throw std::invalid_argument("Invalid HTTP action authentication or content type");
            }
            validateTemplate(text(object, "bodyTemplate", true));
        }
        else
        {
            static const boost::regex tokenPattern(R"(^[0-9]+:[A-Za-z0-9_-]+$)");
            if (!boost::regex_match(text(object, "botToken", true), tokenPattern))
            {
                throw std::invalid_argument("Invalid Telegram bot token");
            }
            text(object, "chatId", true);
            validateTemplate(text(object, "messageTemplate", true));
            const auto mode = text(object, "parseMode");
            if (!mode.empty() && mode != "HTML" && mode != "MarkdownV2")
            {
                throw std::invalid_argument("Invalid Telegram parse mode");
            }
            if (const auto *flag = object.if_contains("disableNotification"); flag && !flag->is_bool())
            {
                throw std::invalid_argument("Invalid Telegram notification flag");
            }
        }
    }
    else
    {
        throw std::invalid_argument("Unknown action type");
    }
    // Drop unused fields, including credentials left over after switching action types.
    const std::set<std::string> common{"name", "description", "type"};
    const std::set<std::string> fields = type == "command"
        ? std::set<std::string>{"path", "args", "appendNewLineToEachArgv", "stdinTemplate"}
        : type == "http_post"
            ? std::set<std::string>{"url", "contentType", "bodyTemplate", "bearerToken", "timeoutSeconds"}
            : std::set<std::string>{"botToken", "chatId", "messageTemplate", "parseMode", "disableNotification", "timeoutSeconds"};
    for (auto field = object.begin(); field != object.end();)
    {
        if (common.count(std::string(field->key())) == 0 && fields.count(std::string(field->key())) == 0)
        {
            field = object.erase(field);
        }
        else
        {
            ++field;
        }
    }
    return Action{std::move(object)};
}

boost::json::value Action::redact(boost::json::value config)
{
    if (config.is_array())
    {
        for (auto &item : config.as_array())
        {
            item = redact(std::move(item));
        }
    }
    else if (config.is_object())
    {
        auto &object = config.as_object();
        object["hasBotToken"] = !text(object, "botToken").empty();
        object["hasBearerToken"] = !text(object, "bearerToken").empty();
        object.erase("botToken");
        object.erase("bearerToken");
    }
    return config;
}

std::string Action::render(const std::string &source, const boost::json::value &event)
{
    validateTemplate(source);
    auto input = std::make_shared<Streams::StreamableString>();
    input->setValue(source);
    Mantids30::Scripting::MantidsLang renderer(input, std::make_shared<boost::json::value>(event));
    RenderedText output;
    if (!renderer.streamTo(&output) || !renderer.writeStatus.succeed || !output.writeStatus.succeed)
    {
        throw std::runtime_error("Action template rendering failed");
    }
    return output.getValue();
}

Mantids30::Client::HTTPv1::Request Action::request(const boost::json::value &event) const
{
    const auto &object = originalConfig.as_object();
    const auto type = text(object, "type");
    Mantids30::Client::HTTPv1::Request result;
    result.method = "POST";
    std::string body;
    if (type == "telegram")
    {
        result.url = "https://api.telegram.org/bot" + text(object, "botToken") + "/sendMessage";
        const auto message = render(text(object, "messageTemplate"), event);
        if (message.empty())
        {
            throw std::runtime_error("Empty Telegram message");
        }
        boost::json::object payload{{"chat_id", text(object, "chatId")}, {"text", message}};
        const auto mode = text(object, "parseMode");
        if (!mode.empty())
        {
            payload["parse_mode"] = mode;
        }
        if (const auto *flag = object.if_contains("disableNotification"))
        {
            payload["disable_notification"] = *flag;
        }
        body = boost::json::serialize(payload);
        result.contentType = "application/json";
    }
    else if (type == "http_post")
    {
        result.url = text(object, "url");
        result.contentType = text(object, "contentType");
        body = render(text(object, "bodyTemplate"), event);
        const auto bearer = text(object, "bearerToken");
        if (!bearer.empty())
        {
            result.headers["Authorization"] = "Bearer " + bearer;
        }
    }
    else
    {
        throw std::invalid_argument("Expected a network action");
    }
    result.content = std::make_shared<Containers::B_Chunks>(body);
    return result;
}

bool Action::post(const boost::json::value &event) const
{
    const auto &object = originalConfig.as_object();
    const auto &timeout = object.at("timeoutSeconds");
    const auto seconds = static_cast<uint32_t>(timeout.is_uint64() ? timeout.as_uint64() : timeout.as_int64());
    Mantids30::Client::HTTPv1::Config config;
    config.connectTimeout = seconds;
    config.readTimeout = seconds;
    config.writeTimeout = seconds;
    config.maxResponseSize = maxRenderedSize;
    config.maxChunkSize = maxRenderedSize;
    auto outgoing = request(event);
    auto responseBody = std::make_shared<Streams::StreamableString>();
    outgoing.responseContent = responseBody;
    const auto response = Mantids30::Client::HTTPv1(config).perform(outgoing);
    if (!response.succeed() || !response.isHTTPSuccess())
    {
        return false;
    }
    if (text(object, "type") == "telegram")
    {
        boost::system::error_code error;
        const auto document = boost::json::parse(responseBody->getValue(), error);
        const auto *ok = document.is_object() ? document.as_object().if_contains("ok") : nullptr;
        return !error && ok && ok->is_bool() && ok->as_bool();
    }
    return true;
}

bool Action::runCommand(const std::string &path, std::vector<std::string> arguments, const std::string &input)
{
    // An anonymous private file avoids pipe deadlocks and SIGPIPE when the child closes stdin.
    const auto closeFile = [](FILE *file) { std::fclose(file); };
    std::unique_ptr<FILE, decltype(closeFile)> source(std::tmpfile(), closeFile);
    if (!source || std::fwrite(input.data(), 1, input.size(), source.get()) != input.size() ||
        std::fflush(source.get()) != 0 || std::fseek(source.get(), 0, SEEK_SET) != 0)
    {
        return false;
    }
    const int descriptor = fileno(source.get());
    // Reserve a descriptor above stderr so CLOEXEC cannot accidentally close stdin.
    const int childInput = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
    if (childInput == -1)
    {
        return false;
    }
    fcntl(descriptor, F_SETFD, FD_CLOEXEC);
    if (arguments.empty())
    {
        arguments.push_back(path);
    }
    std::vector<char *> argv;
    for (auto &argument : arguments)
    {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fileActions;
    if (posix_spawn_file_actions_init(&fileActions) != 0)
    {
        close(childInput);
        return false;
    }
    int error = posix_spawn_file_actions_adddup2(&fileActions, childInput, STDIN_FILENO);
    pid_t child = -1;
    if (error == 0)
    {
        error = posix_spawnp(&child, path.c_str(), &fileActions, nullptr, argv.data(), environ);
    }
    posix_spawn_file_actions_destroy(&fileActions);
    close(childInput);
    if (error != 0)
    {
        return false;
    }
    int status = 0;
    pid_t waited;
    do
    {
        waited = waitpid(child, &status, 0);
    }
    while (waited == -1 && errno == EINTR);
    return waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

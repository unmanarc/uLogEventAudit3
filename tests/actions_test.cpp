/** @file @brief Exercise event templates, stdin delivery, validation and credential persistence. */
#include "rules/action.h"
#include "rules/rules.h"
#include "globals.h"
#include <Mantids30/Core/Memory/streamable_string.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

using namespace UL3::JSONALERT::Filters;
namespace fs = std::filesystem;

/** @brief Fail with a useful diagnostic in release and debug builds. */
static void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

/** @brief Require validation to reject a definition without changing a prior action. */
static void reject(const boost::json::value &definition)
{
    bool rejected = false;
    try
    {
        Action::fromConfig(definition);
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    require(rejected, "Invalid action accepted");
}

/** @brief Obtain request bytes through the same stream interface used by HTTP. */
static std::string body(const Mantids30::Client::HTTPv1::Request &request)
{
    Mantids30::Core::Memory::Streams::StreamableString output;
    require(request.content->streamTo(&output), "Cannot read POST body");
    return output.getValue();
}

/** @brief Check templates, injection boundaries and secret update semantics. */
static void checkActions()
{
    const boost::json::object event{{"message", "Quotes \"\\\n<&> λ {{#print_raw[$secret]!}}"},
        {"items", boost::json::array{boost::json::object{{"name", "first"}}, boost::json::object{{"name", "second"}}}}};
    require(boost::json::parse(Action::render("{{#print_json[$]!}}", event)) == event, "Root context changed");
    require(Action::render("{{#print_raw[$message]!}}", event) == event.at("message").as_string(), "Raw text recursively expanded");
    require(Action::render("{{#foreach [$items as item]}}{{#print_raw[$item.name]!}};{{/foreach}}", event) == "first;second;", "Loop rendering failed");
    require(Action::render("{{#if [IS_EQUAL($items[0].name,\"first\")]}}yes{{/if}}", event) == "yes", "Conditional rendering failed");
    const auto html = Action::render("{{#print_htmlescaped[$message]!}}", event);
    require(html.find("&lt;") != std::string::npos, "HTML escaping failed");

    boost::json::object telegram{{"name", "notify"}, {"type", "telegram"}, {"botToken", "12345:test_token"},
        {"chatId", "@security"}, {"messageTemplate", "{{#print_raw[$message]!}}"}, {"disableNotification", true}};
    auto action = Action::fromConfig(telegram);
    const auto request = action.request(event);
    const auto payload = boost::json::parse(body(request));
    require(payload.at("text") == event.at("message"), "Telegram text was not JSON-escaped");
    require(payload.at("chat_id").as_string() == "@security" && payload.at("disable_notification").as_bool(), "Telegram destination/options lost");
    require(request.url == "https://api.telegram.org/bot12345:test_token/sendMessage", "Wrong Telegram destination");
    const auto publicConfig = Action::redact(action.originalConfig);
    require(!publicConfig.as_object().contains("botToken") && publicConfig.at("hasBotToken").as_bool(), "Token exposed");
    require(Action::fromConfig(publicConfig, &action).originalConfig == action.originalConfig, "Edit lost stored token");
    telegram["botToken"] = "123:replacement";
    auto updated = Action::fromConfig(telegram, &action);
    require(updated.originalConfig.at("botToken").as_string() == "123:replacement", "Token rotation failed");
    for (const auto *invalid : {"bad/token", "123:x?other", "123:x\r\nX"})
    {
        telegram["botToken"] = invalid;
        reject(telegram);
    }
    telegram["botToken"] = "123:test";
    telegram["parseMode"] = "invalid";
    reject(telegram);
    telegram.erase("parseMode");
    for (const boost::json::value timeout : {boost::json::value(0), boost::json::value(uint64_t{0}),
        boost::json::value(-1), boost::json::value(301), boost::json::value("15"), boost::json::value(true)})
    {
        telegram["timeoutSeconds"] = timeout;
        reject(telegram);
    }
    telegram.erase("timeoutSeconds");
    for (const auto *source : {"{{#render[/etc/passwd]!}}", "{{ #include_raw[/etc/passwd]!}}",
        "{{#foreach [$items as item]}}{{#render[/etc/passwd]!}}{{/foreach}}"})
    {
        telegram["messageTemplate"] = source;
        reject(telegram);
    }
    require(!Action::redact(boost::json::array{action.originalConfig}).as_array()[0].as_object().contains("botToken"), "Array redaction failed");
    boost::json::object http{{"name", "post"}, {"type", "http_post"}, {"url", "https://example.com/hook"},
        {"bodyTemplate", "{\"text\":{{#print_json[$message]!}}}"}, {"bearerToken", "secret"}};
    auto post = Action::fromConfig(http);
    const auto httpRequest = post.request(event);
    require(boost::json::parse(body(httpRequest)).at("text") == event.at("message"), "HTTP JSON template corrupted data");
    require(httpRequest.headers.at("Authorization") == "Bearer secret", "HTTP authentication lost");
    require(Action::fromConfig(Action::redact(post.originalConfig), &post).originalConfig == post.originalConfig, "Bearer secret lost on edit");
    http["bearerToken"] = "";
    require(Action::fromConfig(http, &post).originalConfig.at("bearerToken").as_string().empty(), "Cannot remove bearer token");
    for (const auto *url : {"file:///etc/passwd", "https://user:pass@example.com", "https://example.com/\r\nX", "https://example.com/#fragment", "http://example.com:0", "https://example.com:65536", "http://"})
    {
        http["url"] = url;
        reject(http);
    }
    http["url"] = "http://example.com";
    http["bearerToken"] = "secret";
    reject(http);
    reject(nullptr);
    reject(boost::json::object{{"name", "unknown"}, {"type", "unknown"}});
    const auto legacy = Action::fromConfig(boost::json::object{{"name", "legacy"}, {"path", "/bin/true"}});
    require(legacy.originalConfig.at("type").as_string() == "command", "Legacy action not recognized");
    require(boost::json::parse(Action::render(std::string(legacy.originalConfig.at("stdinTemplate").as_string()), event)) == event, "Default stdin lost event");
    bool limited = false;
    try
    {
        Action::render("{{#print_raw[$large]!}}", boost::json::object{{"large", std::string(1024 * 1024 + 1, 'x')}});
    }
    catch (const std::exception &)
    {
        limited = true;
    }
    require(limited, "Rendered template exceeds output limit");
}

/** @brief Verify private persistence, redacted updates, reload and failed-save atomicity. */
static void checkPersistence(const fs::path &directory)
{
    UL3::JSONALERT::Globals::setAppLog(std::make_unique<Mantids30::Runtime::Logs::AppLog>());
    const auto path = directory / "actions.d";
    Rules::setActionsFilePath(path.string());
    const boost::json::object definition{{"name", "telegram"}, {"type", "telegram"}, {"botToken", "123:secret"},
        {"chatId", "@security"}, {"messageTemplate", "{{#print_json[$]!}}"}};
    require(Rules::addAction(definition), "Cannot add Telegram action");
    require(Rules::writeActionsFileConfig(Rules::getCurrentRunningActions()), "Cannot persist action");
    struct stat status{};
    require(stat(fs::directory_iterator(path)->path().c_str(), &status) == 0 && (status.st_mode & 0777) == 0600, "Configuration not private");
    require(Rules::reloadActions(), "Cannot reload action");
    auto publicConfig = Action::redact(Rules::getCurrentRunningAction("telegram"));
    publicConfig.as_object()["description"] = "Updated";
    require(Rules::editAction(publicConfig), "Redacted edit rejected");
    require(Rules::getCurrentRunningAction("telegram").at("botToken").as_string() == "123:secret", "Edit lost secret");
    require(Rules::writeActionsFileConfig(Action::redact(Rules::getCurrentRunningActions())), "Redacted bulk update failed");
    require(Rules::reloadActions(), "Secret did not survive restart");
    const auto saved = Rules::getCurrentRunningActions();
    require(!Rules::writeActionsFileConfig(boost::json::array{definition, definition}), "Duplicate definitions accepted");
    require(!Rules::writeActionsFileConfig(boost::json::array{boost::json::object{{"name", "bad"}, {"type", "bad"}}}), "Invalid bulk save accepted");
    require(Rules::getCurrentRunningActions() == saved, "Failed save changed running actions");
    std::ofstream(path / "invalid.json") << "[42]";
    require(!Rules::reloadActions() && Rules::getCurrentRunningActions() == saved, "Invalid reload lost running actions");
    fs::remove_all(path);
    std::ofstream(path) << "blocked";
    require(!Rules::writeActionsFileConfig(boost::json::array{}), "Failed rename reported success");
    require(Rules::getCurrentRunningActions() == saved, "Failed rename changed running actions");
    require(std::distance(fs::directory_iterator(directory), fs::directory_iterator{}) == 1, "Temporary configuration leaked");
}

/** @brief Run the tests or act as a child command / local HTTP test driver. */
int main(int argc, char **argv)
{
    if (argc == 3 && std::string(argv[1]) == "--stdin-check")
    {
        const std::string input{std::istreambuf_iterator<char>(std::cin), {}};
        return input == std::string(200000, 'x') && std::string(argv[2]) == "literal;$(false)" ? 0 : 1;
    }
    if (argc == 3 && std::string(argv[1]) == "--post-url")
    {
        const auto action = Action::fromConfig(boost::json::object{{"name", "local"}, {"type", "http_post"},
            {"url", argv[2]}, {"timeoutSeconds", 1}, {"bodyTemplate", "{\"message\":{{#print_json[$message]!}}}"}});
        return action.post(boost::json::object{{"message", "Quotes \"\\\n λ"}}) ? 0 : 1;
    }
    std::string pattern = "/tmp/ul3-actions-XXXXXX";
    if (!mkdtemp(pattern.data()))
    {
        return 1;
    }
    const fs::path directory(pattern);
    try
    {
        checkActions();
        checkPersistence(directory);
        require(Action::runCommand("/proc/self/exe", {"action-child", "--stdin-check", "literal;$(false)"}, std::string(200000, 'x')), "Stdin or literal argv changed");
        require(Action::runCommand("/bin/true", {}, std::string(200000, 'x')), "Early stdin close failed");
        require(!Action::runCommand("/nonexistent/command", {}, "input"), "Missing executable reported success");
        require(!Action::runCommand("/bin/false", {}, "input"), "Failed child reported success");
        fs::remove_all(directory);
        std::cout << "PASS: actions, templates, stdin and private credentials\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        fs::remove_all(directory);
        std::cerr << error.what() << '\n';
        return 1;
    }
}

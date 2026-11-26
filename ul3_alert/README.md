# ul3_alert

## Overview
ul3_alert is a system designed to process TCP streams containing JSON data, with one JSON object per line. Its primary function is to filter incoming data and execute actions—most commonly alerting—based on predefined criteria.

## Filtering/Alert Mechanism
The filtering functionality in ul3_alert leverages **JSONPath** integrated with an expression evaluation engine from the **libMantids** library. This combination enables advanced filtering capabilities, including:
- **Advanced Regular Expressions**: Perform complex pattern matching against JSON fields.
- **Conditional Logic**: Evaluate expressions to determine if data meets specific conditions for triggering actions.

## Features
- **Real-Time Processing**: Processes TCP streams in real time, ensuring timely alerting.
- **Flexible Filtering**: Use JSONPath queries combined with libMantids' expressive syntax for precise filtering rules.
- **Modular Actions**: Execute various actions (e.g., sending alerts) based on filter outcomes.

## Usage
1. **Data Ingestion**: Receive a TCP stream where each line is a valid JSON object.
2. **Filter Application**: Apply JSONPath expressions and libMantids evaluations to determine relevance.
3. **Action Execution**: Trigger predefined actions (e.g., alert notifications) when filters match.


## Requirements
- **System Resources**: Adequate memory and CPU to handle expected log throughput.

## Configuration Overview

### Inputs Configuration

The directory **/etc/ulogeventaudit3/ul3_alert/inputs.d/** stores one input object per `.json` file for receiving incoming JSON lines.

#### Example input Configuration File:
```json
{
    "Description": "Alert JSON TCP Input",
    "ListenAddr": "127.0.0.1",
    "ListenPort": 9999,
    "id": "fpL9mjmU"
}
```

- **Description**: Human-readable name for the receptor.
- **ListenAddr**: IP address to bind the listener (e.g., `0.0.0.0` for all interfaces).
- **ListenPort**: TCP port number for incoming connections.
- **id**: Unique identifier for the receptor (required).

Open **Rules & alerts → Inputs** to inspect saved listeners and running client activity on one page. Users with control access can add or remove listeners and reload the service to apply saved changes.

### Actions Configuration

Actions are stored in **/etc/ulogeventaudit3/ul3_alert/actions.d/**. Open
**Rules & alerts → Actions** to add or edit an action and select **Run command**,
**HTTP POST**, or **Telegram message**. Each rule references the action's unique
`name`; the existing rule operations `EXEC` and `EXECANDABORT` work with all three
types. Changes run immediately; use **Save to disk** to retain them after restart.

#### Templates

All body, message and stdin templates use libMantids30's **MantidsLang**, with
the complete incoming event JSON as the root context. For example:

```text
Host: {{#print_raw[$INFO.host]!}}
Event: {{#print_json[$]!}}
{{#foreach [$PATH as path]}}Path: {{#print_raw[$path.name]!}}
{{/foreach}}
```

Use `print_raw` for plain text, `print_htmlescaped` for HTML text, and
`print_json` when inserting a value into JSON (it supplies quotes and escaping).
Conditions and loops are supported. Local file includes/rendering are disabled.
Templates are limited to 64 KiB and rendered output to 1 MiB. Inserted event
values are data and are never interpreted again as templates.

#### Run command

```json
{
  "name": "system-log",
  "type": "command",
  "path": "/usr/bin/logger",
  "args": ["logger", "--tag", "ul3-alert"],
  "stdinTemplate": "{{#print_json[$]!}}\n"
}
```

The rendered input goes to the program's **stdin**, followed by EOF. No shell
is invoked. An omitted `stdinTemplate` sends the whole event as JSON with a
newline; an empty template sends empty input. Arguments are optional; when
omitted, argv[0] defaults to the executable path. Configured arguments include
argv[0] explicitly and keep the legacy percent placeholders in subsequent
arguments: `%N%` (newline), `%#rule.name%` (rule name), `%$.INFO%` (pretty JSON),
`%-.INFO.host%` (compact JSON), `%+.INFO.host%` (uppercase compact JSON), and
`%%` (literal percent). `appendNewLineToEachArgv` preserves its existing behavior.
Legacy actions without `type` load as commands and also receive the default
JSON stdin input.

#### HTTP POST

```json
{
  "name": "webhook",
  "type": "http_post",
  "url": "https://alerts.example.com/events",
  "contentType": "application/json",
  "bodyTemplate": "{\"host\":{{#print_json[$INFO.host]!}},\"event\":{{#print_json[$]!}}}",
  "timeoutSeconds": 15
}
```

The libMantids30 HTTP client sends the rendered body to a fixed HTTP/HTTPS URL.
The content type defaults to `application/json`. Optional `bearerToken`
authentication requires HTTPS. URLs cannot contain embedded credentials or
fragments. HTTPS verifies the certificate chain and hostname; redirects and
retries are not automatic. The timeout (1–300 seconds) applies separately to
connect/read/write operations. Responses are limited to 1 MiB; transport errors,
truncated responses and non-2xx statuses are failures.

#### Telegram message

```json
{
  "name": "security-channel",
  "type": "telegram",
  "botToken": "123456:REPLACE_WITH_BOT_TOKEN",
  "chatId": "@security_channel",
  "messageTemplate": "Alert from {{#print_raw[$INFO.host]!}}\n{{#print_json[$]!}}",
  "parseMode": "",
  "disableNotification": false,
  "timeoutSeconds": 15
}
```

Each action has its own bot token, channel/chat ID and message template. Obtain
a token from BotFather and grant the bot permission to post in the destination
channel. The message is rendered first, then serialized safely as the `text`
field in the JSON request to Telegram's HTTPS `sendMessage` endpoint. An HTTP
success also requires Telegram's JSON `ok` flag to be true.

Plain text is the default. Optional `HTML` and `MarkdownV2` modes require escaping
for that format; `print_htmlescaped` is available for HTML text. Telegram limits
messages to 4096 characters after entity parsing; oversized or incorrectly
formatted messages are rejected by Telegram. See the
[Telegram Bot API](https://core.telegram.org/bots/api#sendmessage).

#### Credentials

The GUI uses password fields. Read APIs omit `botToken` and `bearerToken` and
return `hasBotToken` / `hasBearerToken` flags. Leaving a password field blank on
an edit retains the stored value; enter a new value to rotate it. The HTTP form
also offers **Remove stored bearer token**. Raw API edits preserve omitted
credentials and use an explicit empty bearer token to remove it. Switching types
removes unrelated fields and credentials.

Configuration saves validate the whole array before replacing it atomically
with permissions `0600`. Credentials remain in the private server configuration
file; they are not encrypted at rest. Keep that file and its backups restricted
to the service account. Delivery logs omit URLs, bodies, arguments and exception
text. Failed validation, saves and reloads preserve the running actions.

### Integration Steps:

1. **Configure Alerts:**
   Edit the file located at **/etc/ulogeventaudit3/ul3_alert/config.ini** and set the following parameters under the `[RPCConnector]` section:
   - **AlertsApiKey**: change this for the same PSK in **/etc/ulogeventaudit3/ul3_web/config.ini**

2. **Enable and restart Services:**
   Apply the configuration changes and enable the required services:
    ```bash
    systemctl enable --now ul3_alert
    ```
    and if you want to restart the service:
    ```bash
    systemctl restart ul3_alert
    systemctl restart ul3_web
    ```

3. **Open Firewall Ports Securely**
- If log2json is running on a different host, configure your firewall (e.g., firewalld on RHEL-based systems) to allow incoming traffic on the required ports for ul3_alert.
- **Security Best Practices**:
    - Restrict access to trusted IP addresses/subnets using firewall rules.
    - Ensure all incoming connections pass through a secure network, such as a private VLAN or a restricted VPN tunnel.
    - Avoid exposing the port to the public internet; limit access only to necessary endpoints.

### Configuration directories

Configuration collections use one JSON object per `.json` file. Copy an object file
into the matching directory to reuse it, then reload or restart the service as
required for that collection. Only immediate regular `.json` files are loaded,
in lexicographic filename order. Missing or empty directories contain no entries.
Other extensions (for example `.json.disabled`) are ignored and preserved on save.

The converter uses `inputs.d/`, `outputs.d/` and `syslog_modules.d/`. The alert engine
uses `inputs.d/`, `rules.d/` and `actions.d/`; each rule contains its filter expression.
Nested arrays such as command `args` remain part of their containing object.
Definitions must retain unique IDs or names where required by their schema.

GUI saves generate filenames such as `00000000-alerts.json`, using the entry's ID
or name and a numeric prefix to preserve order, including rule moves. File names
may change on save. Files are private (0600); the service account needs write
access to the parent directory to atomically replace the collection. REST payloads
continue to use arrays. Existing INI path keys now refer to directories; legacy
array files are no longer loaded.

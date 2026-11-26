# Syslog and Auditd Log Processing Application

## Overview
This application is designed to receive auditd logs via a centralized RSYSLOG connection and reassemble fragmented messages into structured JSON format. The processed data can be forwarded in real-time over TCP to downstream systems such as ELK stacks or utilized by uLogEventAudit3 Alerts for generating security notifications and actions.

## Features
- **Configurable Syslog Modules**: Filter other loggers, capture message fields and render JSON templates from the web console.
- **Auditd Log Reception**: Accepts auditd logs through a centralized RSYSLOG setup.
- **Message Reassembly**: Combines fragmented log messages into single, structured JSON objects.
- **Real-Time Forwarding**: Streams processed data over TCP to ELK stacks or other analytics platforms.
- **Integration Support**: Works seamlessly with uLogEventAudit3 Alerts for immediate security action generation.

## Architecture
1. **Input Layer**: RSYSLOG receives incoming auditd logs from other rsyslog's+auditd and routes them to the application.
2. **Processing Pipeline**:
   - **Fragmentation Handling**: Detects and reassembles split messages using sequence identifiers.
   - **JSON Structuring**: Transforms raw log entries into standardized JSON format for easier analysis.
3. **Output Layer**: Sends processed data via TCP streams to designated endpoints like Elasticsearch or security alerting systems.

## Usage
1. Configure RSYSLOG to direct auditd logs to this application's input endpoint.
2. Run the application with appropriate configuration parameters (e.g., listening port, output destinations).
3. Monitor system performance and ensure continuous log flow for real-time analysis.

## Requirements
- **System Resources**: Adequate memory and network bandwidth to handle expected log throughput.
- **RSYSLOG Configuration**: Proper setup to forward auditd messages correctly formatted for this application.

### Compatibility:

Tested on:
- RHEL 7/8/9 and/or compatible systems.

## Configuration Overview

### Rsyslog retransmittion

For rsyslog that aggregate multiple hosts, and you may want to forward everything to uLogEventAudit3, you can configure the file **/etc/rsyslog.d/01-remotes.conf** with the following content:

```conf
template(name="includeLocalTimestampAndSourceIP" type="list") {
    constant(value="<")
    property(name="pri")
    constant(value="><")
    property(name="fromhost-ip")
    constant(value=">")
    property(name="timereported" dateFormat="rfc3164")
    constant(value=" ")
    property(name="hostname")
    constant(value=" ")
    property(name="syslogtag")
    property(name="msg")
    constant(value="\n")
}

# RHEL 7:
:programname, isequal, "audispd"     action(type="omfwd" target="192.168.10.55" port="10514" protocol="tcp" template="includeLocalTimestampAndSourceIP")
# RHEL 8:
:programname, isequal, "audisp-syslog"     action(type="omfwd" target="192.168.10.55" port="10514" protocol="tcp" template="includeLocalTimestampAndSourceIP")
```

This configuration is designed to add the fromhost-ip property/variable to the rsyslog line, so the interpreter can add this into the JSON structure (**INFO.ip**)

> Note: it's important to change the IP address and TCP port with the location where ul3_log2json is listening and reboot the rsyslog after changing the configuration.

### Input configuration

`inputs.json` is a JSON array in `/etc/ulogeventaudit3/ul3_log2json`. Open **Log to JSON → Syslog & auditd inputs** to add, edit or remove TCP listeners. Each entry has a unique `id`, a `Description`, a `ListenAddr` and a numeric `ListenPort` between 1 and 65535.

```json
[
    {
        "id": "rsyslog-auditd",
        "Description": "SYSLOG+AUDITD listener",
        "ListenAddr": "0.0.0.0",
        "ListenPort": 10514
    }
]
```

Use `0.0.0.0` to listen on all IPv4 interfaces or specify a local address. An empty array disables all listeners.

### Syslog modules

Open **Syslog modules** in the web console to add, edit or remove parsers for programs other than `audispd` and `audisp-syslog`. Those two loggers retain the built-in auditd reconstruction path. A module contains a unique name, a process/logger regex, a message regex and a JSON object template.

The page lists saved modules and supports searching by name or regex. Select **New module** or **Edit** to open the editor modal, then **Save and apply** to persist and activate the change. **Cancel** discards the draft. Confirming **Remove** persists and applies the removal immediately. Validation errors keep the editor open for corrections.

Filters use Boost.Regex search semantics. Anchor them with `^` and `$` when the entire value must match. The logger excludes `[PID]`; the message excludes the syslog header. Supported framing includes RFC3164, the forwarded-source format above and RFC5424 version 1, including structured data. Messages use newline-delimited TCP framing.

For example:

- Logger regex: `^sshd$`
- Message regex: `^Accepted (\S+) for (\S+) from (\S+) port ([0-9]+).*$`
- JSON template (extended syntax):

```text
{
  "source": "sshd",
  "method": (string)<$1>,
  "user": (string)<$2>,
  "ip": (string)<$3>,
  "port": (unsigned)<$4>
}
```

A matching message such as `Accepted publickey for alice from 192.0.2.1 port 54321 ssh2` produces:

```json
{"source":"sshd","method":"publickey","user":"alice","ip":"192.0.2.1","port":54321}
```

The libMantids30 `RegexTemplate` renderer handles escaping and typed substitution. Placeholders require an explicit type, such as `(string)<$1>` or `(unsigned)<$4>`. Quotes around a typed placeholder are optional: `"(string)<$1>"` produces the same value as `(string)<$1>`. Bare `"$1"` remains literal text. Unquoted placeholders use the library's extended template syntax; the rendered result is standard JSON. Supported types are `string`, `integer`, `unsigned`, `number`, `boolean` and `null`; native syntax also supports named groups. `$0` selects the whole regex match. Object keys and inline text such as `"user $1"` remain literal. Missing, unmatched or invalid captures keep the library's type defaults (`""`, `0`, `false` or `null`).

Each matching module emits one event through the same bounded distribution queue and configured outputs as auditd. The template controls the complete output object; no metadata is added. Unmatched messages are ignored. Input counters count matched lines once even if multiple modules emit events; the distribution queue tracks event delivery and drops.

**Save and apply** validates all regexes and templates, atomically persists the definitions, and activates them for subsequent messages without restarting. Existing messages finish with their configuration snapshot. Invalid definitions or write failures leave the saved and running configuration intact. The module file defaults to `syslog-modules.json` in the service configuration directory and can be overridden with `[Syslog] ModulesFile`. A missing file means no custom modules. The service account needs write access to that file's parent directory.

The authenticated REST endpoints are `GET /api/v1/log2json/modules` (requires `stats`) and `PUT /api/v1/log2json/modules` (requires `control` and a CSRF token). PUT accepts `{"modules":[{"name":"ssh","loggerRegex":"^sshd$","messageRegex":"...","jsonTemplate":"..."}]}`; success returns `{"ok":true}` and validation or persistence failures return `{"ok":false,"message":"..."}`. Sending an empty modules array removes all custom parsers.

### Output configuration

`outputs.json` is a JSON array in the same directory. Open **Log to JSON → JSON outputs** to add, edit or remove JSON TCP destinations and adjust their queues and connection settings.

```json
[
    {
        "id": "alerts",
        "Description": "Output for Alert Engine",
        "Host": "127.0.0.1",
        "Port": 9999,
        "TransmissionMode": 0,
        "TimeoutSecs": 10,
        "ReconnectSleepTimeInSecs": 3,
        "MaxQueuedItems": 500000,
        "QueuePushTimeoutInMS": 0
    }
]
```

- **id**: Unique, stable destination identity.
- **Description**: Human-readable destination name.
- **Host / Port**: Remote hostname or IP address and TCP port (1–65535).
- **TransmissionMode**: `0` for one JSON event per line (default), `1` for indented JSON.
- **TimeoutSecs**: Positive connection timeout in seconds (default `10`).
- **ReconnectSleepTimeInSecs**: Positive retry delay in seconds (default `3`).
- **MaxQueuedItems**: Positive queue capacity (default `500000`).
- **QueuePushTimeoutInMS**: Enqueue wait in milliseconds; `0` discards overflow immediately (default).

An empty array disables all destinations. For alerts integration, set the destination port to the JSON listener configured in `ul3_alert`.

### Saving and applying endpoint settings

Each inputs/outputs page shows saved entries and running activity together. Running counters continue to describe the active configuration until the service restarts. **New input/output** and **Edit** open a draft; **Cancel** discards it. Saving or confirming removal validates and atomically replaces the corresponding JSON file. Errors leave the saved configuration intact. The service account needs write access to the files' parent directory.

A pending-changes notice includes **Restart service**, which applies both files through the service manager (`Restart=always` in the supplied systemd unit). Processing briefly stops during the restart. When running the binary manually, start it again after requesting a restart. Saving alone leaves active connections, workers and queued events unchanged. Reverting all saved changes clears the notice.

`config.ini` accepts `[Inputs] File=inputs.json` and `[Outputs] File=outputs.json` overrides, relative to the service configuration directory. Both files must exist and contain valid JSON arrays at startup. The old per-endpoint INI directories are no longer loaded.

`GET /api/v1/log2json/inputs/config` and `GET /api/v1/log2json/outputs/config` require `stats`. Each returns `ok`, its endpoint array, `modified` (this file differs from startup) and `restartRequired` (either file differs). `PUT` on the same resources requires `control` and CSRF protection, accepting `{"inputs":[...]}` or `{"outputs":[...]}` and returning the saved configuration. IDs must be unique and descriptions and addresses nonempty. Numeric values must be integers in range. Invalid changes return `{"ok":false,"message":"..."}`. `POST /api/v1/log2json/reload` requires `control` and CSRF protection and requests the service restart.

### Input/Output example:

- An rsyslog input example: [Example audisp-syslog Input](example_input.txt)
- And the output will be something like this (Using TransmissionMode=1): [Example JSON Output](example_output.txt)



### Integration Steps:

1. **Configure log2json:**
   Edit the file located at **/etc/ulogeventaudit3/ul3_log2json/config.ini** and set the following parameters under the `[RPCConnector]` section:
   - **AlertsApiKey**: change this for the same PSK in **/etc/ulogeventaudit3/ul3_web/config.ini**

2. **Enable and restart Services:**
   Apply the configuration changes and enable the required services:
    ```bash
    systemctl enable --now ul3_log2json
    ```
    and if you want to restart the service:
    ```bash
    systemctl restart ul3_log2json
    systemctl restart ul3_web
    ```

3. **Open the Firewall**
    Open the firewall in RHEL (firewalld) for incomming TCP port, Eg. 10514:

    ```bash
    firewall-cmd --zone=public --permanent --add-port 10514/tcp
    # if you require another port, please repeat the last line modifiying the port
    firewall-cmd --reload
    ```

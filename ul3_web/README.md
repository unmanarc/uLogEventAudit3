# ul3_web — uLogEventAudit3 REST console

HTTPS console built with libMantids30 `Server::WebRESTFul::Engine`. uFastAuthD3
provides cookie authentication, JWT validation and APISync scopes and roles.
The console monitors the converter and configures alerts over internal TLS/FastRPC1.

The frontend follows [MANTIDSLANG-GUIDE.md](../../../libs/libMantids30/docs/MANTIDSLANG-GUIDE.md): server-rendered
HTML5 shell and partials, with plain JavaScript and jQuery fetching REST/JSON data.
There is no bundler or browser framework. All scripts, styles and images are
served locally; Bootstrap and jQuery are included in the repository.

See [BACKEND-GUIDE.md](../../../libs/libMantids30/docs/BACKEND-GUIDE.md) for a service built from scratch and
[WEBAPP-PROGRAMMING-STYLE.md](../../../libs/libMantids30/docs/WEBAPP-PROGRAMMING-STYLE.md) for the current
implementation conventions and agent verification requirements.

## Files and assets

- `var/www/ulogeventaudit3/webroot/`: document root, pages and `resources.conf`.
- `webroot/includes/layout/`: shared header, footer and sidebar.
- `webroot/mods/alerts/` and `webroot/mods/log2json/`: each page contains its own
  section markup and REST behavior. Only shared layout and navigation use includes.
- `var/www/ulogeventaudit3/global/assets/`: local vendor and application assets,
  mounted at `/assets` by `OverlappedDirectories`.
- `var/www/package.json`, lockfile and `build-assets.js`: reproducible vendor copies.

Refresh the local vendor assets with Node.js/npm:

```bash
cd ul3_web/var/www
npm ci --ignore-scripts
npm run build-assets
```

Only the `ulogeventaudit3` tree is installed. `node_modules` and npm tooling stay
outside the document root. CMake checks that required vendor files exist.
Source maps and vendor licenses are included locally as well.

The console uses a light header for logo contrast, a navy sidebar and teal
accents from the existing SVG brand. `console.css` defines the responsive theme;
`console.js` owns page titles, active navigation, the mobile menu, table search,
request status and removal dialogs. These are plain local assets: no extra
dependencies or asset build are needed when editing the application UI.

Module statistics have an explicit Refresh button and show the last successful update.
The System resources page polls automatically. Lists distinguish loading, empty and failed
requests. Editor forms use native validation and keyboard submission, with
write controls disabled while saving. Rule/action changes still need Save to
disk; input changes still need a service reload, as explained in the UI.

Pages compose partials using `{{#render[/includes/…]!}}`. Session identity uses
`print_htmlescaped`, and permissions use exact scope comparisons. Write controls
are omitted for readers. Keep JSONExprEval atomic expressions free of spaces,
for example `IS_EQUAL($scope,"control")`: the current evaluator rejects the
space after a comma shown in some guide examples.

`resources.conf` denies direct access to partials and policy files, processes
pages as MANTIDSLANG, requires a session for assets, and redirects unauthenticated
requests to `/auth/retokenize/`. Editor pages require both `stats` and `control`.
The `/auth` proxy serves the platform's local `/auth/assets/js/auth.js`; that
helper owns refresh, logout and platform inactivity behavior. Audit code does
not decode or store JWTs. Each mutation obtains the current CSRF value from the
session endpoint, so a platform token refresh cannot leave a stale CSRF value.

## Configuration and uFastAuthD3

Configuration is `/etc/ulogeventaudit3/ul3_web/webserver.conf` in Boost INFO format.

1. Register the application `ULOGEVENTAUDIT3` in uFastAuthD3 and provide its API key
   through `UL3_API_KEY` (or `Authentication.APIKey`). `UL3_APP` can override the app ID.
2. Configure the APISync host/port, private CA and HTTPS listener certificates.
3. Configure the `/auth` proxy to the authentication handler and the `/login`
   proxy to the embedded login portal. Both forward the application API key;
   `/login` uses `TransformCookiePath` for portal cookies. Configure login
   origins and the callback URL for your actual audit hostname. The sample
   uses `audit.localhost:33000`.
4. Startup publishes `stats` and `control`, the roles `AUDIT_READER` (stats) and
   `AUDIT_ADMIN` (stats + control), and the `LOGIN` activity through APISync.
   Assign the appropriate role to application users.
5. Match the `RPCServer` API keys with each backend's `[RPCConnector]` credentials.
   Configure trusted RPC certificates on both backends.

The web root now ends in `/webroot`; `/assets` must map to `global/assets`.
Upgrade the deployed web tree and `ResourcesPath`/`OverlappedDirectories` together.
Merge those path changes into existing configuration during upgrades.

## JSON API contract

Endpoints use `/api/v1/`, cookie JWT authentication and JSON results. `GET session`
returns user, CSRF value, remaining lifetime and version. Two POST endpoints
forward calls to the internal FastRPC services:

- `POST remote.stats` requires `stats` and calls `stats.<method>`.
- `POST remote.control` requires `control` plus `X-CSRF-Token` and calls `control.<method>`.

Both accept a JSON body such as:

```json
{"target":"ALERTS","method":"ReadRunningRules","payload":{}}
```

`target` is `WEB`, `ALERTS` or `LOG2JSON`; `method` is a bare RPC method name,
without a namespace or dots. The endpoint supplies the namespace so a stats
request cannot select a control method. `payload` is optional and defaults to
an empty object. The response is the backend result. Callbacks request `Cache-Control: no-store`;
see the [runtime cache compatibility note](../../../libs/libMantids30/docs/MANTIDSLANG-GUIDE.md#browserapi-contract)
for the installed HTTP writer's override behavior.
For example, saving rules uses `remote.control` with `method: "SaveRunningRules"`;
reading converter inputs uses `remote.stats`, `target: "LOG2JSON"` and `method: "Inputs"`.
`WEB` dispatches through the local FastRPC method registry; the other targets use
their existing internal connections. No per-method HTTP route registration is needed.

## OS resource dashboard and sensing API

Each executable owns one joined background sensor thread using libMantids30's
`Core::Helpers::ResourceMonitor`. It samples three independent resolutions:
`3s` (3 seconds), `1m` (60 seconds), and `60m` (3600 seconds). Each resolution
has its own CPU and I/O baseline; coarse rates cover the whole coarse interval.
The first sample is immediate, with null CPU percentages and I/O rates until
the next sample of that resolution. Missing OS counters remain JSON `null`.

Retention defaults to 300 samples **per resolution, per process**: approximately
15 minutes, 5 hours, and 12.5 days. The bounded histories are held in memory and
reset on process restart. Set the following in both backends' `config.ini` files:

```ini
[ResourceMonitor]
SamplesPerSeries=300
```

The web service keeps its existing Boost INFO `webserver.conf` format:

```text
ResourceMonitor
{
    SamplesPerSeries 300
}
```

The valid range is 1..3000; invalid configuration fails startup. Restart the
corresponding service after changing its retention. Each service can retain a
different number of samples. OS reads run outside the history mutex; API reads
copy stored measurements and do not trigger sensing. Shutdown wakes the sensor
immediately rather than waiting for its next deadline.

All three FastRPC instances register `stats.ResourceMonitor`, accepting
`{"resolution":"3s"}` (or `1m` / `60m`). Invalid arguments return
`{"error":"invalid_resolution"}`. Authenticated readers with `stats` scope use
`POST /api/v1/remote.stats` with this body:

```json
{"target":"WEB","method":"ResourceMonitor","payload":{"resolution":"3s"}}
```

Use `ALERTS` or `LOG2JSON` as the target for the other processes. Each response
requests `Cache-Control: no-store` through the callback (subject to the runtime
compatibility note above) and contains one process history:

```json
{
  "component": "web",
  "resolution": "3s",
  "intervalSeconds": 3,
  "capacity": 300,
  "running": true,
  "latest": null,
  "samples": []
}
```

The dashboard requests all three targets concurrently and combines their results
in the browser. Disconnected services return HTTP 503; RPC failures return HTTP
502. Failed components are marked unavailable while successful components keep
updating. Polling cycles do not overlap, and obsolete resolution results are discarded.
`latest` is the last entry in `samples`, or null before the first sample.
Timestamps are Unix milliseconds for
display; rate calculations use actual monotonic elapsed time. Each sample includes:

| Fields | Meaning / units |
| --- | --- |
| `timestamp`, `processId`, `logicalProcessorCount` | Capture time and native process/host identity |
| `processCpuPercent`, `systemCpuPercent` | Percent of total online host CPU capacity (0..100) |
| `processCpuSeconds`, `processUptimeSeconds`, `systemUptimeSeconds` | Seconds |
| `residentMemoryBytes`, `peakResidentMemoryBytes`, `virtualMemoryBytes`, `privateCommitBytes` | Bytes; platform-dependent counters can be null |
| `processMemoryPercent` | RSS as a percentage of host physical RAM |
| `systemMemory` | `totalBytes`, `availableBytes`, `usedPercent`, or null |
| `threadCount`, `handleCount` | Threads and Linux file descriptors / Windows handles |
| `processIO` | Cumulative `readBytes`, `writeBytes`, `storageReadBytes`, `storageWriteBytes`, plus corresponding `*BytesPerSecond` rates; or null |

Logical I/O includes cached transfers (Linux `rchar`/`wchar`). Storage counters
follow OS semantics, are unavailable on Windows, and do not represent device-wide
throughput or network-interface traffic. CPU and memory capacity use host totals,
without container/affinity adjustments. System measurements belong to each process's
host; hosts need not be the same machine.

The [System resources page](var/www/ulogeventaudit3/webroot/mods/system/resources.html),
linked directly below Overview in the sidebar, has one resolution selector and six
local SVG charts comparing Web,
Alerts and Log2JSON: CPU, resident memory, logical I/O, storage I/O, threads, and
descriptors/handles. I/O charts use solid reads and dashed writes. Pointer readouts,
per-component PID/uptime/host details, null gaps and disconnected/stale indicators
help inspect the measurements. Polling never overlaps, ignores obsolete resolution
responses and pauses in hidden tabs. It runs every 3 seconds at `3s` resolution
and every minute for coarser views, with backoff on errors.

[Build and test instructions](../BUILD.md) include real-service integration checks.
The fixture checks rendering, permissions and TLS/FastRPC1; the deployed
uFastAuthD3 login portal, callback, refresh and logout need a deployment check.

# Migration to libMantids30 and uFastAuthD3

All three applications now use libMantids30 and C++17. The browser uses REST;
`uanlz_alert` and `uanlz_log2json` keep outbound TLS connections to the web
service through libMantids30 FastRPC1. Migrate all three executables together.
Event JSON, rule/action files and the converter's input/output configuration
formats are preserved. Events, configuration, expressions and RPC payloads use
Boost.JSON throughout, with the libMantids30 JSON helpers. Empty collections
are JSON arrays (`[]`); legacy `null` configuration files load as empty lists.
Configuration files must contain a JSON array or legacy `null`.

## Authentication and configuration

1. Register `UAUDITANALYZER` in **uFastAuthD3**. Enable APISync and configure the
   application's API key, callback URI and cookie paths for this deployment.
   With the sample hostnames, the login return address is
   `https://audit.localhost:33000/`, and the authentication handler is proxied
   under `/auth`. The callback URI is
   `https://audit.localhost:33000/auth/api/v1/callback`.
2. Replace `uanlz_web/config.ini` with the supplied
   `uanlz_web/webserver.conf` (Boost INFO syntax). The `LoginRPCClient` section,
   PSK login client and old authentication resources have been removed.
3. Set `UAUDIT_API_KEY` in the web service environment, or
   `Authentication.APIKey` in the config. `UAUDIT_APP` optionally overrides
   `Authentication.AppName`. Restrict the config to mode 0600.
4. Configure `WebService.APISync` for the uFastAuthD3 APISync service, and
   `WebService.Proxies./auth` for its session authentication handler. The
   defaults are TLS ports 7081 and 7080, respectively. Set CA paths and hostnames
   to match the deployed certificates. The web listener uses `Listener_TLS`,
   matching the current libMantids30 builder.
5. Set `WebService.API.Origins`, `Login.Origins`, `Login.RedirectURL` and the
   `/login` redirection for the actual deployment. `redirectURI` in the login
   URL is the Base64 encoding of the application's return URL. Update the
   hostname and that encoded value together.
6. Choose distinct random `RPCServer.AlertsApiKey` and
   `RPCServer.Log2JSONApiKey`. Set the corresponding `RPCConnector` values in
   the two backends. The converter now uses **Log2JSONApiKey**, correcting the
   previous AlertsApiKey setting. Copy the internal server CA into each
   backend's configured `CAFile`, and use a `RemoteAddr` matching the server
   certificate's SAN. Hostname verification is enabled.
7. Deploy the new web resources and restart all three services. Assign the
   synchronized `AUDIT_READER` or `AUDIT_ADMIN` role to the intended users in
   uFastAuthD3. These grant `stats` and `stats`+`control`, respectively.

The browser obtains session details from `/api/v1/session`, renews access
through `/auth/api/v1/refreshAccessToken`, and logs out through
`/auth/api/v1/logout`. Mutations require the `X-CSRF-Token` value returned by
`session`. Authentication uses the validated `AccessToken` cookie.
Unauthenticated resource requests go to `/auth/retokenize/` so an existing
refresh session can be renewed before returning to the portal.

The console loads Bootstrap 5.3.3 and jQuery 3.7.1 from their CDNs; deployments
without Internet access should vendor these files and update the two layout
includes. The old web server previously supplied these assets internally.

## REST API

Base URL: `/api/v1/`. GET routes require `stats`; mutations require `control`.
The route list is in `uanlz_web/src/rest_routes.inc`. Targets and internal RPC
method names cannot be selected by callers.

| Resource | HTTP methods | Purpose |
| --- | --- | --- |
| `session` | GET | User, version, remaining lifetime and CSRF value |
| `alerts/stats` | GET | Alert engine statistics |
| `alerts/rules` | GET, POST, PUT, DELETE | List, add, edit, remove running rules |
| `alerts/actions` | GET, POST, PUT, DELETE | List, add, edit, remove running actions |
| `alerts/rules/item`, `alerts/actions/item` | GET | Read an individual rule/action |
| `alerts/rules/modified`, `alerts/actions/modified` | GET | Unsaved changes |
| `alerts/rules/config`, `alerts/actions/config` | GET, PUT | Read/write persisted configuration |
| `alerts/rules/save`, `alerts/actions/save` | POST | Save running configuration |
| `alerts/rules/reload`, `alerts/actions/reload` | POST | Reload configuration |
| `alerts/rules/up`, `alerts/rules/down` | POST | Reorder a rule |
| `alerts/inputs` | GET | Running inputs |
| `alerts/inputs/config` | GET, POST, DELETE | Read, add or remove input configuration |
| `alerts/reload` | POST | Request backend restart by the service manager |
| `log2json/hosts` | GET | Events per host |
| `log2json/distribution` | GET | Distribution workers |
| `log2json/inputs`, `log2json/outputs` | GET | Input/output statistics |

Mutations accept JSON bodies with the previous backend payload schema.
GET arguments use a URL-encoded JSON query, as required by libMantids30:
`alerts/rules/item?%7B%22id%22%3A0%7D`. For example:

```bash
curl --cacert ca.crt --cookie 'AccessToken=YOUR_TOKEN' \
  --get --data-urlencode '{"id":0}' \
  https://audit.localhost:33000/api/v1/alerts/rules/item
```

Responses contain the backend JSON value directly. A disconnected backend
returns 503; a failed internal RPC returns 502. libMantids30 currently returns
403 for missing JWT authentication and 401 for missing scopes.

See [BUILD.md](BUILD.md) for building and the temporary integration test.

## C++17 ownership and concurrency

The build uses ISO C++17 with compiler extensions disabled. libMantids30 remains
responsible for sockets, acceptors, line parsing, FastRPC1, JSON helpers, expression
evaluation and the underlying thread-safe queues.

Audit text and decoded bytes are `std::string` values, including embedded NULs;
class records are stored directly in their event. Single-owner objects such as
pending events, rules, actions, loggers and configured inputs/outputs use
`std::unique_ptr`. Host processing and statistics/GC snapshots share hosts through
`std::shared_ptr`, so queue waits and per-host locks do not hold the host registry
mutex. Sockets keep the shared ownership required by libMantids30. Borrowed
class/client pointers and library callback contexts remain non-owning; wrapping
these in unrelated shared owners would introduce double deletion or cycles.

Queues use `Mantids30::Core::Threads::Safe::Queue` directly. Producers retain a
`unique_ptr` while pushing and release it only after a successful push transfers
ownership to Mantids30. Rejected items are destroyed by their producer's smart
pointer. Consumers immediately adopt popped pointers into `unique_ptr`. Scoped locks release on exceptions.
Connection parsers live on the callback stack and unregister on scope exit.
Counters use atomic exchange when publishing interval totals; event ages use a
monotonic clock and formatted event timestamps use thread-safe time conversion.
Socket address decoding uses bounded copies into aligned structures.

Actions preserve the configured executable, argv[0], PATH lookup and inherited
standard streams. Their argument storage uses string/vector values; process
waiting handles interruptions and reports launch/wait errors. AppSpawn's direct
path and argv[0] conventions differ, so this path retains execvp semantics.

The existing service-lifetime detached workers remain. A future graceful shutdown
needs coordinated input cancellation, queue draining, RPC stop and worker joins;
smart pointers alone do not provide that lifecycle.

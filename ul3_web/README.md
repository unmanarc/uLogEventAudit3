# ul3_web — uLogEventAudit3 REST console

HTTPS console built with libMantids30 `Server::WebRESTFul::Engine`. uFastAuthD3
provides cookie authentication, JWT validation and APISync scopes and roles.
The console monitors the converter and configures alerts over internal TLS/FastRPC1.

The frontend follows [MANTIDSLANG-GUIDE.md](../MANTIDSLANG-GUIDE.md): server-rendered
HTML5 shell and partials, with plain JavaScript and jQuery fetching REST/JSON data.
There is no bundler or browser framework. All scripts, styles and images are
served locally; Bootstrap and jQuery are included in the repository.

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
3. Configure the `/auth` proxy and the login portal URL, origins and callback URL
   for your actual audit hostname. The sample uses `audit.localhost:33000`.
   Update the base64 `redirectURI` in `/login` when changing the callback URL.
4. Startup publishes `stats` and `control`, the roles `AUDIT_READER` (stats) and
   `AUDIT_ADMIN` (stats + control), and the `LOGIN` activity through APISync.
   Assign the appropriate role to application users.
5. Match the `RPCServer` API keys with each backend's `[RPCConnector]` credentials.
   Configure trusted RPC certificates on both backends.

The web root now ends in `/webroot`; `/assets` must map to `global/assets`.
Upgrade the deployed web tree and `ResourcesPath`/`OverlappedDirectories` together.
Merge those path changes into existing configuration during upgrades.

## REST contract

Endpoints use `/api/v1/`, cookie JWT authentication and JSON results. `GET session`
returns user, CSRF value, remaining lifetime and version. Read operations require
`stats`; writes require `control` plus `X-CSRF-Token`. JSON GET arguments use the
Mantids30 convention `?` followed by URL-encoded JSON. Mutations send a JSON body.

The server's complete allowlist is [rest_routes.inc](src/rest_routes.inc).
Resources cover `alerts/rules`, `alerts/actions`, `alerts/inputs`, their
configuration, save/reload operations, `alerts/stats`, and the `log2json` hosts,
inputs, outputs and distribution statistics. There is no browser RPC gateway.

[Build and test instructions](../BUILD.md) include real-service integration checks.
The fixture checks rendering, permissions and TLS/FastRPC1; the deployed
uFastAuthD3 login portal, callback, refresh and logout need a deployment check.

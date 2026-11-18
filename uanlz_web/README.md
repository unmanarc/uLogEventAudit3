# uanlz_web — uAuditAnalyzer3 REST console

HTTPS console built with libMantids30 `Server::WebRESTFul::Engine`, using
uFastAuthD3 for login, JWT validation and APISync scopes/roles. It monitors the
converter and configures the alert engine over internal TLS/FastRPC1 links.

Configuration is `/etc/uauditanalyzer/uanlz_web/webserver.conf` in Boost INFO
format. The supplied sample defines the HTTPS listener, APISync, `/auth` proxy,
login portal, origins, TLS certificates and service API keys.

Follow [the migration guide](../MIGRATION.md) to register the application,
configure credentials and certificates, and assign `AUDIT_READER` or
`AUDIT_ADMIN`. [Build instructions](../BUILD.md) include an integration test.

The REST API is under `/api/v1/`; its resources and request format are listed
in the migration guide. The former `/japi_exec` interface and legacy
uFastAuthD LoginRPCClient are removed.

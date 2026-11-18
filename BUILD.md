# Building uAuditAnalyzer3

Requires CMake 3.16+, C++17, libMantids30 3.0, Boost 1.75+ (JSON, regex,
thread), OpenSSL, and pkg-config. The three executables use the
`Mantids30_*` pkg-config modules; the old `mdz_*` libraries are no longer used.

For a libMantids30 installation under `~/Devel/ROOT`:

```bash
export PKG_CONFIG_PATH="$HOME/Devel/ROOT/share/pkgconfig:$PKG_CONFIG_PATH"
cmake -S . -B build/mantids30 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_SYSCONFDIR=/etc
cmake --build build/mantids30 -j
```

Build and install `~/Devel/Projects/libs/libMantids30` first if it is not already
installed. Set `PKG_CONFIG_PATH` to that installation's pkg-config directory.
For an installation outside the system linker search path, set
`LD_LIBRARY_PATH` to its library directory when running the executables.

To review installation without changing the system:

```bash
DESTDIR="$PWD/build/stage" cmake --install build/mantids30
```

The web resources install into `/var/www/uauditweb`; configuration goes into
`/etc/uauditanalyzer` with the options above. Service unit files install into
`lib/systemd/system` under the configured prefix.
Configure credentials, certificates, origins and login URLs before enabling
services; see [MIGRATION.md](MIGRATION.md).

## Integration test

```bash
python3 tests/rest_integration.py --build-dir build/mantids30 --library-dir "$HOME/Devel/ROOT/lib"
node tests/web_client.test.js
```

The test uses the three real executables, a temporary APISync HTTP fixture and
new TLS certificates. It checks authentication, scopes, CSRF, rendering,
service connections, JSON arguments, configuration writes, 64-bit event IDs
and action argument path substitutions. It opens only
local ports and modifies temporary files. Python 3 and the OpenSSL CLI are
required; the browser client check requires Node.js. The fixture does not replace testing the configured uFastAuthD3 login
portal, callback and refresh flow in a deployment.

# Building uLogEventAudit3

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

The frontend ships local Bootstrap and jQuery assets. To reproduce them:

```bash
cd ul3_web/var/www
npm ci --ignore-scripts
npm run build-assets
```

Run CMake from the repository root. The frontend uses MANTIDSLANG templates
under `webroot/` and local vendor files under `global/assets/`. Node.js is only
needed to refresh vendor files; there are no runtime CDN requests.

The web resources install into `/var/www/ulogeventaudit3`; configuration goes into
`/etc/ulogeventaudit3` with the options above. Service unit files install into
`lib/systemd/system` under the configured prefix.
Configure credentials, certificates, origins and login URLs before enabling
services; see [ul3_web/README.md](ul3_web/README.md).

## Local development installation

After building into `build/devel-root`, run:

```bash
python3 tools/setup-dev.py
```

This prepares `~/Devel/ROOT/etc/ulogeventaudit3`, links the web tree and binaries
to this checkout, generates a local TLS certificate and RPC keys, and creates
`~/Devel/ROOT/bin/ul3_{web,alert,log2json}-dev` launchers. Existing configuration
and credentials are preserved. Use `--prefix` or `--build-dir` to override paths.
The prefix must already contain libMantids30 and the uFastAuthD3 CA at
`etc/ufastauthd3/tls/ca.crt`.

The web listens at `https://localhost:33100`, RPC at `33101`, syslog input at
`11514`, and JSON alert input at `19999`, all on loopback. Rules and actions start
empty. Register `ULOGEVENTAUDIT3` in your local uFastAuthD3 and configure its API
key before starting the web. The generated `etc/ulogeventaudit3/README.txt`
contains launch commands, callback settings and certificate details. Optional
systemd user unit files are placed under the prefix without enabling them.

## C++ regression tests

CTest builds `audit_modernization_test` when `BUILD_TESTING` is enabled (the
default). It checks queue ownership on rejection, pop and destruction; repeated
parsing and copying of binary audit variables; fragmented EXECVE records;
separate PATH records; and malformed socket addresses.

```bash
LD_LIBRARY_PATH="$HOME/Devel/ROOT/lib" ctest --test-dir build/mantids30 --output-on-failure
```

For memory and undefined-behavior checks on these paths:

```bash
PKG_CONFIG_PATH="$HOME/Devel/ROOT/share/pkgconfig:$PKG_CONFIG_PATH" \
cmake -S . -B build/modernization-sanitizers -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/modernization-sanitizers --target audit_modernization_test -j
LD_LIBRARY_PATH="$HOME/Devel/ROOT/lib" \
  ctest --test-dir build/modernization-sanitizers --output-on-failure
```

These flags instrument the application test sources; installed libMantids30
libraries retain their own build flags.

## Static analysis

Generate the compilation database and run clang-tidy from the repository root:

```bash
cmake -S . -B build/mantids30 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
run-clang-tidy-21 -p build/mantids30 -j 4
```

The repository configuration covers application, common and test headers. If
Clang selects an incomplete GCC installation and cannot find standard headers,
select the installed GCC toolchain explicitly, for example:

```bash
run-clang-tidy-21 -p build/mantids30 -j 4 \
  -extra-arg=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15
```

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

## Source documentation

Function contracts use Doxygen in headers; source-only helpers and Mantids30
lifecycle overrides are documented beside their implementation. With Doxygen
installed, generate the HTML reference with:

```bash
doxygen Doxyfile
```

The entry point is `build/doxygen/html/index.html`. See [CONTRIBUTING.md](CONTRIBUTING.md)
for naming, formatting, ownership and documentation conventions.

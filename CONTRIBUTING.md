# C++ conventions

Use ISO C++17 and keep libMantids30 as the implementation of its existing
networking, parsing, RPC, JSON helper, expression evaluation and queue facilities.

## Naming and layout

- Types use `PascalCase`: `AuditEvent`, `TcpServer`, `RuleAction`.
- Functions, parameters, local variables and members use `lowerCamelCase`:
  `getMainConfig`, `recordData`, `hostsMutex`. Use verbs for operations and names
  that describe the data for variables. Avoid type prefixes such as `s`, `j`, `b`
  and `v` and names such as `tmp` when their role can be stated directly.
- Treat acronyms as words in application identifiers: `getJson`, `setRemoteIp`,
  `runRpcClient`. Keep library identifiers and required overrides exactly as
  defined by libMantids30, including `_config`, `_start` and `processParsedLine`.
- Include units in durations and limits: `pushTimeoutMs`, `maxEventAgeSeconds`.
  Name mutexes after the state they protect: `hostsMutex`, `rulesMutex`.
- Preserve published JSON members, RPC method names, configuration keys and
  command-line options when renaming internal symbols.
- Existing constants, macros and enum values use `UPPER_SNAKE_CASE`. Preserve
  existing namespaces and source file paths.
- Group public operations first, private helpers next and state members last.
  Use the repository's `.clang-format` for indentation and line wrapping.

## Documentation

Document declared functions with Doxygen at their declaration in the header.
Document source-only helpers and application lifecycle hooks beside their
implementation. Describe the operation, parameters and non-obvious return values;
include ownership, borrowed lifetimes, lock requirements and non-returning worker
loops where they affect callers. Use `@pre`, `@note` and `@tparam` as appropriate.

Keep code comments in English, consistent with the source. Explain why a lock is
released, how fragmented audit records are assembled or how placeholders avoid
recursive expansion. Avoid narrating assignments or repeating the function name.

Queues use `Mantids30::Core::Threads::Safe::Queue` directly. A producer retains
its `unique_ptr` until a successful push, then calls `release()`. A consumer
immediately adopts a popped pointer into `unique_ptr`. Borrowed references and
callback contexts do not acquire a second owner.

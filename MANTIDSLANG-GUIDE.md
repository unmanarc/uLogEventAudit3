# Building a Web Frontend with MANTIDSLANG — Complete Guide for AI Agents

> **Target:** Any AI agent tasked with producing a working **HTML5 frontend** that runs on a
> `Mantids30` (libMantids30) web server.
> **Scope:** Frontend-first. The C++ backend is summarized at a high level only.
> **Domain used in examples:** a generic **task list / notes app**. No other domain is implied.

---

## Table of Contents

1. [What MANTIDSLANG is and the paradigm](#1-what-mantidslang-is-and-the-paradigm)
2. [Project layout (web side)](#2-project-layout-web-side)
3. [MANTIDSLANG tag reference (the core)](#3-mantidslang-tag-reference-the-core)
4. [HTML5 frontend conventions](#4-html5-frontend-conventions)
5. [Node.js for frontend dependencies](#5-nodejs-for-frontend-dependencies)
6. [`resources.conf` — access control for the web root](#6-resourcesconf--access-control-for-the-web-root)
7. [Backend, high level (C++ summary)](#7-backend-high-level-c-summary)
8. [Step-by-step build checklist](#8-step-by-step-build-checklist)
9. [Minimal copy-paste sample project (tasks app)](#9-minimal-copy-paste-sample-project-tasks-app)
10. [Do / Don't rules](#10-do--dont-rules)
11. [Quick reference index](#11-quick-reference-index)

---

## 1. What MANTIDSLANG is and the paradigm

### 1.1 What it is

`MANTIDSLANG` is a **server-side, JSON-context-driven HTML5 template language** implemented
inside the `Mantids30` web engine (library `Mantids30_Scripting_MantidsLang`, rendered by
`Mantids30::Server::WebRESTFul`). When a browser requests a page, the web server:

1. resolves the file from the **webroot** (and **overlapped directories**),
2. applies the **`resources.conf`** filter (access control + processing mode),
3. if the rule says `PROCESS_AS: MANTIDSLANG`, it runs the file through the template engine,
4. the engine scans the file for `{{ ... }}` tags and replaces them using a **JSON context**,
5. the rendered HTML is streamed back to the browser.

The template engine is a *streaming* interpreter: it reads the source file, tokenizes it into
`DATA` chunks and `SUBTAG` blocks, then walks the tokens while evaluating each tag against the
current JSON context.

### 1.2 The paradigm (important — read this)

The overall architecture is a **hybrid**, *not* a client-side framework:

| Layer | Technology | Runs |
|-------|-----------|------|
| **Shell / chrome** (navbar, layout, access-gated sections) | **MANTIDSLANG** templates (HTML5 + `{{ }}` tags) | **Server** |
| **Data & behavior** | **vanilla JavaScript + jQuery** calling a **REST/JSON API** | **Browser** |
| **UI kit** | Bootstrap 5, jQuery, DataTables (vendored via Node.js/npm) | **Browser** |
| **API** | `Mantids30` REST engine, JWT cookie auth, JSON in/out | **Server (C++)** |

Consequences an AI agent must respect:

- **There is no React / Vue / Angular.** Do not introduce a client framework, a build
  bundler (webpack/vite), or transpilation. The only "build" step is `npm run build-assets`,
  which copies vendor files (see §5).
- **Data flows two ways:**
  - *Server → page:* via the **JSON context** rendered by MANTIDSLANG tags (session user,
    client IP, roles, etc.).
  - *Browser → data:* via `$.ajax(...)` / `fetch(...)` calls to `${API_BASE}/<endpoint>`
    that return JSON. This is the SPA-like part, but done with plain JS.
- **Access control happens twice:** once on the server (which files need a session, which are
  processed as templates) via `resources.conf`, and once per API endpoint via JWT **scopes**.
- **HTML escaping matters.** Use `print_htmlescaped` for any user-controlled value that is
  interpolated into the server-rendered shell.

### 1.3 Request lifecycle (end to end)

```
Browser
  │  GET /index.html
  ▼
Mantids30 web engine
  │  1. resolve path in webroot / overlapped dirs
  │  2. evaluate resources.conf rules (first match wins)
  │       └─ requireSession?  →  check JWT cookie session
  │       └─ actions: DENY | REDIRECT | ACCEPT | PROCESS_AS{MANTIDSLANG}
  │  3. if PROCESS_AS MANTIDSLANG:
  │       build JSON context { session, client, server, script, request, software }
  │       run MantidsLang template over the file
  │  4. stream rendered body
  ▼
Browser (HTML5)
  │  DOMContentLoaded / body onload
  │  jQuery + Bootstrap + DataTables take over
  │  $.ajax( API_BASE + "/tasks", ... )   ← REST/JSON API
  ▼
Mantids30 REST API  (JWT cookie auth, per-endpoint scopes, JSON in/out)
```

**Caching note (from the engine source):** a template file that *contains* `{{` and `}}` is
treated as **dynamic** — the server sends `Cache-Control: no-store` (no caching). A file
without tags is treated as static and revalidated by ETag/Last-Modified. So: **do not put
`{{ }}` tags in static assets** (CSS/JS/fonts), and keep dynamic pages in `.html` files.

---

## 2. Project layout (web side)

The web assets live under a `var/www/` tree. This is the layout you should reproduce:

```
<project>/
├── CMakeLists.txt              # C++ service (backend) — see §7
├── etc/
│   └── <ProjectName>/
│       ├── webserver.conf      # engine config: TLS, ResourcesPath, API, proxies
│       └── tls/
│           ├── ca.crt
│           ├── snakeoil.crt    # dev certificate
│           └── snakeoil.key    # dev key
├── var/
│   ├── log/                    # engine + app logs (created at runtime)
│   └── www/
│       ├── package.json        # Node.js dependency manifest (see §5)
│       ├── package-lock.json
│       ├── node_modules/       # VENDOR SOURCE — git-ignored, NOT served
│       ├── global/
│       │   └── assets/         # SERVED shared assets (copied from node_modules)
│       │       ├── js/         #   bootstrap.bundle.min.js, jquery.min.js, dataTables.*
│       │       ├── css/        #   bootstrap.min.css, dataTables.*.css
│       │       ├── webfonts/
│       │       └── types/
│       └── webroot/            # DOCUMENT ROOT for pages
│           ├── index.html      # main page (processed as MANTIDSLANG)
│           ├── resources.conf  # access-control filter (see §6)
│           ├── assets/         # local (per-project) static assets
│           │   └── css/main.css
│           └── includes/       # server-side partials (MANTIDSLANG `render`)
│               ├── sections/
│               │   ├── inc.section-dashboard.html
│               │   └── inc.section-tasks.html
│               └── modals/
│                   ├── inc.task.html
│                   └── inc.confirm_delete.html
```

Key concepts:

- **`webroot/`** is the document root. Page files (`index.html`, `login.html`) live here.
- **`includes/`** holds **partials** pulled into pages with the `render` tag. These are
  typically *denied/redirected* by `resources.conf` so they can only be reached through a
  parent page (see §6).
- **`global/assets/`** is the *shared* static asset tree. It is exposed at `/assets` through
  an **overlapped directory** mapping in `webserver.conf`:
  ```
  OverlappedDirectories
  {
      /assets
      {
          Path "var/www/<ProjectName>/global/assets"
      }
  }
  ```
  So a page references assets as `href="assets/css/bootstrap.min.css"` and the engine
  resolves it to the `global/assets` directory, *not* `webroot/assets`.
- **`webroot/assets/`** can still hold project-local assets that are *not* shared globally.

> **Rule of thumb:** pages + includes in `webroot/`; shared vendored libraries in
> `global/assets/` (served at `/assets`).

---

## 3. MANTIDSLANG tag reference (the core)

### 3.1 Tag grammar

- A tag is delimited by `{{` and `}}`. The engine's regex is **non-greedy**: `\{\{([^}]*?)\}\}`,
  i.e. everything between the first `{{` and the *nearest* `}}`.
- Two shapes:
  - **Self-closing** — ends with `!` before `}}`. Executes once and emits output inline.
    Example: `{{#render[/includes/sections/inc.section-tasks.html]!}}`
  - **Block** — no `!`; has a matching closing tag `{{/NAME}}`. Everything between is the
    block body, processed repeatedly or conditionally.
    Example: `{{#if [IS_EQUAL($session.isActive, true)]}} ... {{/if}}`
- The **action name** is the first word inside the tag (before `[` or a space/tab).
- A **JSON path** inside `[ ... ]` must start with `$` and uses dot notation,
  e.g. `$session.user`. The leading `$` is required.

### 3.2 JSON context available to templates

The server builds this JSON context before rendering (verified from
`apiserver_clienthandler_filehandling.cpp`). Reference any member with a leading `$`:

| Path | Meaning |
|------|---------|
| `$session.isActive` | `true` if the user has an active (authenticated) session |
| `$session.user` | authenticated username (only present when authenticated) |
| `$session.domain` | auth domain |
| `$session.roles` | array of role ids for the session |
| `$session.scopes` | array of scope ids for the session |
| `$session.isImpersonation` | whether the session is impersonated |
| `$session.halfID` | session half-id (only present when authenticated) |
| `$session.impersonator` | impersonating username (only present when impersonated) |
| `$client.tlsCN` | TLS common name of the client |
| `$client.ip` | client IP address |
| `$client.userAgent` | browser User-Agent |
| `$server.unixTime` | server time (unix seconds) |
| `$script.fullpath` / `$script.relativePath` | path of the file being rendered |
| `$request.get` / `$request.post` | GET / POST variable maps as JSON objects |
| `$software.version` / `$software.name` / `$software.description` | app metadata |

> Note: the *browser-side* JavaScript does **not** read this context through MANTIDSLANG for
> app data. App data comes from the REST API. The context is for the **server-rendered shell**:
> greeting the user, showing role-gated UI, echoing the client IP in debug, etc.

### 3.3 Self-closing actions (emit output inline)

All follow the pattern `{{#<action>[<arg>]!}}`.

| Tag | What it emits |
|-----|---------------|
| `{{#print_json[$path]!}}` | Raw **JSON** representation of the value at `$path` (good for debug / `<pre>`). |
| `{{#print_raw[$path]!}}` | **Raw string** value of the leaf at `$path` (no escaping). Use for values you control. |
| `{{#print_htmlescaped[$path]!}}` | Value at `$path` **HTML-escaped**. **Use this for any user-controlled text.** |
| `{{#include_raw[/relative/path]!}}` | Inserts the **raw file bytes** of the given path (relative to the document root). No templating on the included file. |
| `{{#render[/relative/path]!}}` | Includes the file **and runs it through the MANTIDSLANG engine** with the same JSON context. **This is how you compose pages from partials.** |

**Examples:**

```html
<!-- Greet the logged-in user, HTML-escaped (safe) -->
<h1>Welcome, {{#print_htmlescaped[$session.user]!}}</h1>

<!-- Server-render a partial (section) into the page -->
{{#render[/includes/sections/inc.section-tasks.html]!}}

<!-- Server-render a partial (modal) into the page -->
{{#render[/includes/modals/inc.task.html]!}}
```

> **`include_raw` vs `render`:** use `render` for partials that themselves contain `{{ }}`
> tags (sections, modals). Use `include_raw` for plain snippets that must be injected verbatim
> (e.g. a pre-built chunk that must not be re-templated).

### 3.4 Block tags (conditional / iteration)

#### `foreach` — iterate a JSON array

Syntax (verified from the renderer source):

```
{{#foreach [$<json.path> as <varName>]}}
    ... body using <varName> ...
{{/foreach}}
```

- `$<json.path>` **must** start with `$` (a path without the `$` is rejected and the block is
  skipped — nothing emitted).
- The path must resolve to a **JSON array** (each element is bound to `<varName>`). It may also
  resolve to a **JSON object**, in which case each iteration binds a `{ "key": …, "value": … }`
  object to `<varName>`.
- Each iteration binds the element to `<varName>`, which becomes available in the body
  as a JSON path `$<varName>`.
- If the path is missing or resolves to `null`, the block is **skipped** (nothing emitted).

**Example — list the session scopes:**

```html
<ul class="scopes">
{{#foreach [$session.scopes as scope]}}
    <li>{{#print_htmlescaped[$scope]!}}</li>
{{/foreach}}
</ul>
```

#### `if` — conditional

Syntax (verified from the renderer source):

```
{{#if [<json-expression>]}}
    ... body ...
{{/if}}
```

- The expression is evaluated by the **`JSONExprEval`** engine. It is **not** a general
  boolean expression: only the following **function-style operators** are supported, and any
  other syntax (a bare path, `==`, `!=`, `>`, `<`, …) fails to compile and the block is
  **silently omitted** (nothing emitted).
- Supported operators (verified from `atomicexpression.cpp`):
  | Operator | Meaning |
  |----------|---------|
  | `IS_EQUAL(a, b)` | `a` equals `b` |
  | `!IS_EQUAL(a, b)` | `a` is **not** equal to `b` (negation prefix `!`) |
  | `REGEX_MATCH(a, b)` | `a` matches regex `b` |
  | `CONTAINS(a, b)` | `a` contains `b` |
  | `STARTS_WITH(a, b)` / `ENDS_WITH(a, b)` | prefix / suffix match |
  | `IS_NULL(a)` | `a` resolves to nothing |
  - Prefix `i` (e.g. `iIS_EQUAL`) makes the comparison case-insensitive; prefix `!` negates.
  - Combine multiple atomic expressions with `&&` / `||` (do not mix both in one expression)
    and parenthesize with `(...)`; a `!(...)` sub-expression negates it.
- **Operands:** a JSON path operand is written with a **leading `$`** (e.g. `$session.user`).
  A quoted string literal (`"admin"`) or a number has **no** `$`. A bare identifier with no `$`
  is *not* recognized as a path and resolves to nothing (so `IS_EQUAL(session.isActive, true)`
  is always false).
- If the expression is truthy, the body is processed and emitted; otherwise it is omitted.
- The expression text lives **inside the brackets**.

**Examples:**

```html
<!-- Only render the "sign out" link when authenticated -->
{{#if [IS_EQUAL($session.isActive, true)]}}
    <a href="#" onclick="logout()">Sign out</a>
{{/if}}

<!-- Show a banner for non-admin users (negated IS_EQUAL — there is no "!=" operator) -->
{{#if [!IS_EQUAL($session.user, "admin")]}}
    <span>(standard user view)</span>
{{/if}}
```

> **Nesting:** block tags can nest. A nested `if`/`foreach` inside another block is its own
> `SUBTAG` and is processed with the inherited (or extended, for `foreach`) JSON context.

### 3.5 Practical rules for templates

1. **Escape user data** in the shell with `print_htmlescaped`. Reserve `print_raw` for values
   you fully control (static labels, your own ids).
2. **Compose pages** from partials with `render`. Keep each partial self-contained (its own
   `<div>` and, for sections, its own `<script>`).
3. **Do not put `{{ }}` in static assets** (`.css`/`.js`/fonts). They would be flagged dynamic
   and cached with `no-store`, and the engine would try to parse them.
4. **The `$` prefix depends on the tag.** For `print_*` / `include_raw` / `render` the JSON
   path **must** start with `$` (a missing `$` makes the tag silently no-op). For `if` /
   `foreach` *operands*, `$` marks a JSON-path operand, but string literals and numbers have
   no `$` — so a rule like `IS_EQUAL($session.user, "admin")` mixes both.
5. **Most specific rule first** in `resources.conf`. The engine evaluates rules in order and
   stops at the first **terminal** action (`ACCEPT` / `REDIRECT` / `DENY`); a non-terminal
   `PROCESS_AS` only sets the mode and does not stop the scan. A page that should be templated
   needs `PROCESS_AS MANTIDSLANG` followed by `ACCEPT`.

---

## 4. HTML5 frontend conventions

### 4.1 Page skeleton

The main page is a **single HTML5 document** that (a) loads the UI kit, (b) loads the shared
auth script, (c) renders sections/modals via MANTIDSLANG, and (d) defines the shared JS
helpers. A representative structure:

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>My Tasks App</title>

    <!-- UI kit (served from /assets -> global/assets) -->
    <link href="assets/css/bootstrap.min.css" rel="stylesheet">
    <script src="assets/js/jquery.min.js"></script>
    <script src="assets/js/dataTables.min.js"></script>
    <script src="assets/js/dataTables.bootstrap5.js"></script>
    <link href="assets/css/dataTables.bootstrap5.min.css" rel="stylesheet">

    <!-- optional icon font from a CDN -->
    <link href="https://cdn.jsdelivr.net/npm/bootstrap-icons@1.11.1/font/bootstrap-icons.css" rel="stylesheet">

    <!-- project-local styles -->
    <link href="assets/css/main.css" rel="stylesheet">

    <!-- shared auth helpers (provided by the platform auth proxy) -->
    <script src="auth/assets/js/auth.js"></script>

    <script>
        const API_BASE = '/api/v1';        // base URL for all REST calls
        let tasksCache = [];               // client-side cache of the last fetch
    </script>
</head>
<body>
    <header>
        <nav class="navbar navbar-expand-md navbar-dark bg-dark fixed-top">
            <!-- brand + nav links -->
        </nav>
    </header>

    <main class="main-content">
        <div class="toast-container" id="toastContainer"></div>

        <!-- server-rendered sections (partials) -->
        {{#render[/includes/sections/inc.section-dashboard.html]!}}
        {{#render[/includes/sections/inc.section-tasks.html]!}}
    </main>

    <!-- server-rendered modals (partials) -->
    {{#render[/includes/modals/inc.task.html]!}}
    {{#render[/includes/modals/inc.confirm_delete.html]!}}

    <!-- UI kit bundle (must come after DOM it controls) -->
    <script src="assets/js/bootstrap.bundle.min.js"></script>

    <script>
        // ===== shared helpers (see 4.3) =====
        function showToast(message, type = 'success') { /* ... */ }
        function commonFunctionError(xhr) { /* ... */ }
        function formatDate(s) { /* ... */ }
    </script>
</body>
</html>
```

### 4.2 Section & modal pattern

- A **section** partial = one `<div>` (the panel) + one `<script>` (its logic). The page shows
  one section at a time by toggling visibility, or renders all and switches with a sidebar.
- A **modal** partial = a Bootstrap `<div class="modal">` form used for create/edit, plus a
  small confirm-delete modal.

**Section partial example** (`includes/sections/inc.section-tasks.html`): a card with a table
and a script that does `$.ajax` against `${API_BASE}/tasks`, builds the rows, and sets user
text safely with `.text()`:

```html
<div id="section-tasks" class="section">
    <div class="card">
        <div class="card-header d-flex justify-content-between align-items-center">
            <h5 class="mb-0">My Tasks</h5>
            <button class="btn btn-sm btn-primary" onclick="showTaskModal()">
                <i class="bi bi-plus-lg"></i> New Task
            </button>
        </div>
        <div class="card-body">
            <table id="tasksTable" class="table table-striped">
                <thead>
                    <tr><th>#</th><th>Title</th><th>Done</th><th>Created</th><th></th></tr>
                </thead>
                <tbody id="tasksTableBody"></tbody>
            </table>
        </div>
    </div>
</div>

<script>
    function loadTasks() {
        $.ajax({
            url: `${API_BASE}/tasks`,
            type: 'GET',
            contentType: 'application/json',
            data: JSON.stringify({}),
            success: function (response) {
                tasksCache = response;
                let html = response.length === 0
                    ? '<tr><td colspan="5" class="text-center text-muted">No tasks found</td></tr>'
                    : response.map(function (t) {
                          return `<tr id="task-row-${t.id}">
                                      <td>${t.id}</td>
                                      <td id="task-title-${t.id}"></td>
                                      <td>${t.done ? 'yes' : 'no'}</td>
                                      <td>${formatDate(t.created_at)}</td>
                                      <td>
                                          <button class="btn btn-sm btn-outline-danger"
                                                  onclick="deleteTask(${t.id})"><i class="bi bi-trash"></i></button>
                                      </td>
                                  </tr>`;
                      }).join('');
                $('#tasksTableBody').html(html);
                // set text safely (escape) — never build HTML with raw user text
                response.forEach(function (t) {
                    $(`#task-title-${t.id}`).text(t.title);
                });
            },
            error: commonFunctionError
        });
    }

    function createTask(title) {
        $.ajax({
            url: `${API_BASE}/tasks`, type: 'POST', contentType: 'application/json',
            data: JSON.stringify({ title: title }),
            success: function () { showToast('Task created'); loadTasks(); },
            error: commonFunctionError
        });
    }

    function deleteTask(id) {
        $.ajax({
            url: `${API_BASE}/tasks`, type: 'DELETE', contentType: 'application/json',
            data: JSON.stringify({ id: id }),
            success: function () { showToast('Task deleted'); loadTasks(); },
            error: commonFunctionError
        });
    }

    // run on page load
    $(function () { loadTasks(); });
</script>
```

**Modal partial example** (`includes/modals/inc.task.html`):

```html
<div class="modal fade" id="taskModal" tabindex="-1" aria-hidden="true">
    <div class="modal-dialog"><div class="modal-content">
        <div class="modal-header">
            <h5 class="modal-title" id="taskModalTitle">Add Task</h5>
            <button type="button" class="btn-close" data-bs-dismiss="modal"></button>
        </div>
        <div class="modal-body">
            <input type="hidden" id="taskEditMode" value="create">
            <div class="mb-3">
                <label for="taskTitle" class="form-label">Title</label>
                <input type="text" class="form-control" id="taskTitle" required>
            </div>
        </div>
        <div class="modal-footer">
            <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Cancel</button>
            <button type="button" class="btn btn-primary" onclick="saveTask()">Save</button>
        </div>
    </div></div>
</div>

<script>
    function showTaskModal() {
        $('#taskModalTitle').text('Add Task');
        $('#taskEditMode').val('create');
        $('#taskTitle').val('');
        new bootstrap.Modal(document.getElementById('taskModal')).show();
    }

    function saveTask() {
        const title = $('#taskTitle').val().trim();
        if (!title) { return; }
        createTask(title);
        new bootstrap.Modal(document.getElementById('taskModal')).hide();
    }
</script>
```



### 4.3 Shared JS helpers (define once in the main page)

```javascript
function showToast(message, type = 'success') {
    const toastId = 'toast-' + Date.now();
    const bgClass = type === 'success' ? 'bg-success' : type === 'error' ? 'bg-danger' : 'bg-warning';
    const toastHtml = `
        <div id="${toastId}" class="toast align-items-center text-white ${bgClass} border-0" role="alert">
            <div class="d-flex">
                <div class="toast-body">${$('<div>').text(message).html()}</div>
                <button type="button" class="btn-close btn-close-white me-2 m-auto" data-bs-dismiss="toast"></button>
            </div>
        </div>`;
    $('#toastContainer').append(toastHtml);
    const toast = new bootstrap.Toast(document.getElementById(toastId));
    toast.show();
    setTimeout(() => $(`#${toastId}`).remove(), 5000);
}

function commonFunctionError(xhr) {
    let msg = 'Request failed';
    try {
        const r = JSON.parse(xhr.responseText);
        if (r && r.message) { msg = r.message; }
    } catch (e) { /* keep default */ }
    showToast(msg, 'error');
}

function formatDate(s) {
    if (!s) { return ''; }
    const d = new Date(s);
    return isNaN(d.getTime()) ? s : d.toLocaleString();
}
```

### 4.4 REST call conventions (browser side)

- Base URL: `const API_BASE = '/api/v1';` (version segment matters; the engine registers
  endpoints per API version).
- **Every request/response is JSON.** Send `contentType: 'application/json'` and
  `data: JSON.stringify({...})`.
- **Auth is a cookie** (JWT) managed by the platform auth proxy (`auth/assets/js/auth.js`).
  The browser does not manage tokens manually for page calls; `$.ajax` sends the cookie
  automatically on same-origin requests.
- The **`logout()`** function and any account-display helpers come from `auth.js`.
- Map HTTP verbs to actions: `GET` read, `POST` create, `PUT` update, `DELETE` delete,
  `PATCH` partial state change (e.g. approve).

---

## 5. Node.js for frontend dependencies

Node.js is used **only to download and vendor front-end libraries** into the served asset
tree. It is *not* used for bundling, transpiling, or a dev server.

### 5.1 `package.json`

Located in `var/www/`. It declares the vendor dependencies and the copy scripts.

```json
{
  "name": "www",
  "version": "1.0.0",
  "description": "Frontend asset vendor for the tasks app",
  "scripts": {
    "build-assets": "npm run make-dirs && npm run copy-bootstrap && npm run copy-jquery && npm run copy-datatables && npm run copy-datatables-bs5",
    "make-dirs": "mkdir -pv global/assets/js global/assets/css global/assets/webfonts global/assets/types",
    "copy-bootstrap": "cp -r node_modules/bootstrap/dist/* global/assets/",
    "copy-jquery": "cp -r node_modules/jquery/dist/* global/assets/js/",
    "copy-datatables": "cp -r node_modules/datatables.net/js/ global/assets/js/ && cp -r node_modules/datatables.net/types/ global/assets/types/",
    "copy-datatables-bs5": "cp -r node_modules/datatables.net-bs5/js/ global/assets/js/ && cp -r node_modules/datatables.net-bs5/css/ global/assets/css/ && cp -r node_modules/datatables.net-bs5/types/ global/assets/types/"
  },
  "dependencies": {
    "bootstrap": "^5.3.3",
    "jquery": "^3.7.1",
    "datatables.net": "^2.3.8",
    "datatables.net-bs5": "^2.3.8"
  }
}
```

> The committed manifest in an existing project (e.g. MyApp `var/www/package.json`)
> may differ slightly (older `datatables.net`/`datatables.net-bs5` copy paths, a `types/`
> directory that is created manually). Treat the block above as the canonical, tidy form.

### 5.2 Commands

Run these **inside `var/www/`** (the directory containing `package.json`):

```bash
cd var/www

# First time (or after adding a dependency):
npm install

# To update existing packages to latest in-range versions:
npm update jquery bootstrap datatables.net datatables.net-bs5

# Download is done; now vendor into the served tree:
npm run build-assets
```

What each step does:

- `npm install` / `npm update` → populate `node_modules/` (this directory is **git-ignored**
  and **never served**).
- `npm run build-assets` → `make-dirs` creates `global/assets/{js,css,webfonts,types}`, then
  each `copy-*` script copies the needed `dist` files from `node_modules/*` into
  `global/assets/`. The result is what the browser actually loads via `/assets/...`.

### 5.3 What the page references after the build

```html
<link  href="assets/css/bootstrap.min.css" rel="stylesheet">
<script src="assets/js/jquery.min.js"></script>
<script src="assets/js/bootstrap.bundle.min.js"></script>
<script src="assets/js/dataTables.min.js"></script>
<script src="assets/js/dataTables.bootstrap5.js"></script>
<link  href="assets/css/dataTables.bootstrap5.min.css" rel="stylesheet">
```

These resolve (via the overlapped directory) to
`var/www/<Project>/global/assets/css/bootstrap.min.css`, etc.

> **Do not** reference `node_modules/...` directly in HTML, and **do not** serve
> `node_modules/`. Always reference `/assets/...` after running `npm run build-assets`.

---

## 6. `resources.conf` — access control for the web root

`resources.conf` (at the root of the webroot) is a **JSON array** of rules. The engine
**evaluates them in order; the first rule whose `uriRegexs` matches the requested URI wins**
and its `actions` are applied in order.

### 6.1 Rule shape

```json
{
    "_comment": "human note",
    "uriRegexs": [ "^/login.*$", "^/assets.*$" ],   // one or more regexes (match any)
    "requireSession": true,                          // if true, an active session is required
    "actions": [
        { "type": "PROCESS_AS", "mode": "MANTIDSLANG" },
        { "type": "ACCEPT" }
    ]
}
```

### 6.2 Actions

| `type` | Effect |
|--------|--------|
| `DENY` | Reject the request (403). |
| `REDIRECT` | Redirect to `location` (optionally with `statusCode`). |
| `ACCEPT` | Serve the resource (optionally with a `statusCode`, default 200). |
| `PROCESS_AS` | Set the processing mode (`MANTIDSLANG`, `HTMLIENGINE`, or `RAW`). Must be paired with a terminal action like `ACCEPT`. |
| `ADD_HEADERS` / `REPLACE_HEADERS` | Modify response headers. |

`requireSession: true` means the rule is **considered** only when the request has an
authenticated session; when no session is active the rule simply does not match, so the scan
continues and the later catch-all rule (which redirects to the auth proxy) is what fires.

### 6.3 Worked example (mirrors the real file, generic)

```json
[
    {
        "_comment": "Never expose the filter config itself.",
        "uriRegexs": ["^/resources.conf$"],
        "actions": [ { "type": "DENY" } ]
    },
    {
        "_comment": "Includes are server-side only; redirect any direct request.",
        "uriRegexs": ["^/includes.*$"],
        "actions": [ { "type": "REDIRECT", "location": "/" } ]
    },
    {
        "_comment": "HTML pages require a session and are templated with MANTIDSLANG.",
        "uriRegexs": ["^/.*html$"],
        "requireSession": true,
        "actions": [ { "type": "PROCESS_AS", "mode": "MANTIDSLANG" },
                     { "type": "ACCEPT" } ]
    },
    {
        "_comment": "Static assets (shared /assets and local) require a session, served raw.",
        "uriRegexs": ["^/assets.*$", "^/local.*$", "^/$"],
        "requireSession": true,
        "actions": [ { "type": "ACCEPT" } ]
    },
    {
        "_comment": "Login is always accessible (public).",
        "uriRegexs": ["^/login.*$"],
        "requireSession": false,
        "actions": [ { "type": "ACCEPT" } ]
    },
    {
        "_comment": "Anything else without a session -> go to the auth proxy.",
        "uriRegexs": ["^.*$"],
        "actions": [ { "type": "REDIRECT", "location": "/auth/retokenize/" } ]
    },
    {
        "_comment": "Final fallback: deny.",
        "uriRegexs": ["^.*$"],
        "actions": [ { "type": "DENY" } ]
    }
]
```

**Order matters.** Notice the catch-all `^.*$` redirect is placed *before* the final deny,
so unauthenticated users are sent to the auth proxy rather than getting a bare 403. Keep the
most specific rules first.

---

## 7. Backend, high level (C++ summary)

You only need to understand this enough to **contract** with it. The service is a single
C++17 executable built with **CMake**, linking `libMantids30` components via **pkg-config**.

### 7.1 What it does

- Starts a **HTTPS REST engine** (`Mantids30::Server::WebRESTFul::Engine`), TLS mandatory.
- Loads `webserver.conf` (listener, TLS cert/key, thread pool, `ResourcesPath`, overlapped
  directories, `API` origins, `/auth` proxies, logging).
- Initializes **SQLite3** (in-memory base + attached `.db` files) and creates tables from a
  list of `CREATE TABLE` statements.
- Registers **API endpoints** per version: `engine->endpointsHandler[1] = registerAPIEndpoints();`
- Publishes **scopes / roles / activities** to the auth system via `APISync`
  (`updateAccessControlContext(...)`), so JWT tokens carry the scopes your endpoints require.
- Runs until shutdown.

### 7.2 Endpoint contract (what your frontend relies on)

Each endpoint is a C++ function with this signature:

```cpp
APIReturn apiTaskCreate(void *,
                        const API::RESTful::RequestContext &params,   // JSON body + JWT
                        Sessions::ClientDetails &clientDetails);      // IP, agent, ...
```

- **Input:** `params.inputJSON` is the JSON request body (e.g. `{"title":"..."}`).
- **Identity:** `params.jwtToken->getSubject()` is the authenticated username.
- **Output:** return an `APIReturn` — either a JSON value (default HTTP 200) or an explicit
  status like `S_201_CREATED`, `S_400_BAD_REQUEST`, `S_500_INTERNAL_SERVER_ERROR`.
- **Registration** maps an HTTP method + path + **security requirement** + **required scopes**:

```cpp
endpoints->addEndpoint(HTTP::Method::GET,    "tasks", SecReq::JWT_COOKIE_AUTH, {},            nullptr, &apiTaskRead);
endpoints->addEndpoint(HTTP::Method::POST,   "tasks", SecReq::JWT_COOKIE_AUTH, {"TASK_WRITE"}, nullptr, &apiTaskCreate);
endpoints->addEndpoint(HTTP::Method::DELETE, "tasks", SecReq::JWT_COOKIE_AUTH, {"TASK_WRITE"}, nullptr, &apiTaskDelete);
```

So the browser call `GET /api/v1/tasks` (with the auth cookie) is answered by `apiTaskRead`,
and `POST /api/v1/tasks` requires the `TASK_WRITE` scope on the token. **Your frontend must
only call endpoints the current role is entitled to** (the server enforces this, but a good
frontend hides what it can't use).

### 7.3 How the template JSON context is built

For each templated request the server fills the context object (§3.2) from the live session:
`session.isActive`, `session.user`, `session.roles`, `session.scopes`, `client.*`, `server.*`,
`script.*`, `request.*`, `software.*`. This is why templates can show the user name and
role-gated UI without any extra API call.

### 7.4 `webserver.conf` essentials

```
WebService
{
    Listener_TLS { ListenPort 16443  ListenAddr "0.0.0.0"
        Protocol TLS
        TLS { CertFile "etc/<P>/tls/snakeoil.crt"  KeyFile "etc/<P>/tls/snakeoil.key" } }
    Threads { MaxConcurrentClients 500  MaxConcurrentConnectionsPerIP 256 }
    ResourcesPath "var/www/<P>/webroot"
    OverlappedDirectories { /assets { Path "var/www/<P>/global/assets" } }
    API { Origins "https://<host>:16443" }
    Proxies { "/auth" { UseTLS true  RemoteHost "login.localhost"  RemotePort 7080 ... } }
    Logs  { Dir "var/log/<P>"  File "webservice.log"  ... }
}
```

The `/auth` proxy forwards the browser to the platform login, and the auth cookie it sets is
what `JWT_COOKIE_AUTH` endpoints validate.

---

## 8. Step-by-step build checklist

Use this ordered checklist to produce a working frontend.

1. **Create the web tree**
   - `var/www/webroot/` (pages + includes + `resources.conf`),
   - `var/www/global/assets/{js,css,webfonts,types}` (empty, to be filled),
   - `var/www/package.json`.
2. **Write `package.json`** with the UI-kit `dependencies` and the `build-assets` copy
   scripts (§5.1).
3. **Vendor the libraries**
   - `cd var/www && npm install && npm run build-assets`.
   - Verify `global/assets/css/bootstrap.min.css` and `global/assets/js/jquery.min.js` exist.
4. **Write the main page** `webroot/index.html` (§4.1):
   - reference assets as `assets/...` (they map to `global/assets`),
   - load `auth/assets/js/auth.js`,
   - define `API_BASE`, caches, and shared helpers (`showToast`, `commonFunctionError`,
     `formatDate`),
   - `{{#render[.../inc.section-*.html]!}}` for each section,
   - `{{#render[.../inc.*.html]!}}` for each modal,
   - load `bootstrap.bundle.min.js` before the inline helpers.
5. **Write each section partial** (`includes/sections/inc.section-<name>.html`, §4.2):
   - a `<div>` panel + a `<script>` that does `$.ajax` against `${API_BASE}/<resource>` and
     renders rows into a table; build row HTML with template literals but set user text with
     `.text()` (escaped).
6. **Write each modal partial** (`includes/modals/inc.<name>.html`, §4.2): a Bootstrap modal
   form + show/save logic that calls the create/update endpoint.
7. **Write `webroot/resources.conf`** (§6.3): deny `resources.conf`, redirect `includes/`,
   templated `*.html` with `requireSession`, accept `/assets`, public `/login`, catch-all
   redirect to `/auth/retokenize/`, final deny.
8. **Wire the backend contract** (if you own the C++ side, §7): register one endpoint set per
   resource with the right `SecReq` and scopes, and add the matching scopes/roles to
   `appScopes()` / `appRoles()`.
9. **Verify**
   - open `https://<host>:<port>/login` (public) → log in → land on `index.html`,
   - confirm sections render, `loadTasks()` populates the table, create/delete work,
   - confirm `includes/` and `resources.conf` are **not** directly reachable,
   - confirm unauthenticated requests are redirected to the auth proxy.

---

## 9. Minimal copy-paste sample project (tasks app)

A complete, consistent, **generic** frontend. Replace `<Project>` with your app name.

### 9.1 `var/www/package.json`

```json
{
  "name": "www",
  "version": "1.0.0",
  "description": "Frontend asset vendor for the tasks app",
  "scripts": {
    "build-assets": "npm run make-dirs && npm run copy-bootstrap && npm run copy-jquery",
    "make-dirs": "mkdir -pv global/assets/js global/assets/css global/assets/webfonts",
    "copy-bootstrap": "cp -r node_modules/bootstrap/dist/* global/assets/",
    "copy-jquery": "cp -r node_modules/jquery/dist/* global/assets/js/"
  },
  "dependencies": {
    "bootstrap": "^5.3.3",
    "jquery": "^3.7.1"
  }
}
```

```bash
cd var/www && npm install && npm run build-assets
```

### 9.2 `var/www/webroot/index.html`

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>My Tasks</title>

    <link href="assets/css/bootstrap.min.css" rel="stylesheet">
    <script src="assets/js/jquery.min.js"></script>

    <link href="assets/css/main.css" rel="stylesheet">
    <script src="auth/assets/js/auth.js"></script>

    <script>
        const API_BASE = '/api/v1';
        let tasksCache = [];
    </script>
</head>
<body>
    <nav class="navbar navbar-dark bg-dark">
        <div class="container">
            <span class="navbar-brand mb-0 h1"><i class="bi bi-list-check"></i> My Tasks</span>
            <span class="text-white">
                {{#if [IS_EQUAL($session.isActive, true)]}}
                    signed in as {{#print_htmlescaped[$session.user]!}}
                    <a href="#" onclick="logout()" class="text-white">Sign out</a>
                {{/if}}
            </span>
        </div>
    </nav>

    <main class="container my-4">
        <div class="toast-container" id="toastContainer"></div>
        {{#render[/includes/sections/inc.section-tasks.html]!}}
        {{#render[/includes/modals/inc.task.html]!}}
    </main>

    <script src="assets/js/bootstrap.bundle.min.js"></script>
    <script>
        function showToast(message, type = 'success') {
            const id = 'toast-' + Date.now();
            const bg = type === 'error' ? 'bg-danger' : 'bg-success';
            $('#toastContainer').append(`
                <div id="${id}" class="toast align-items-center text-white ${bg} border-0">
                    <div class="d-flex">
                        <div class="toast-body">${$('<div>').text(message).html()}</div>
                        <button type="button" class="btn-close btn-close-white me-2 m-auto" data-bs-dismiss="toast"></button>
                    </div>
                </div>`);
            new bootstrap.Toast(document.getElementById(id)).show();
            setTimeout(() => $(`#${id}`).remove(), 5000);
        }
        function commonFunctionError(xhr) {
            let m = 'Request failed';
            try { const r = JSON.parse(xhr.responseText); if (r && r.message) m = r.message; } catch (e) {}
            showToast(m, 'error');
        }
        function formatDate(s) { return s ? (new Date(s).toLocaleString()) : ''; }
    </script>
</body>
</html>
```

### 9.3 `var/www/webroot/includes/sections/inc.section-tasks.html`

```html
<div id="section-tasks">
    <div class="card">
        <div class="card-header d-flex justify-content-between align-items-center">
            <h5 class="mb-0">Tasks</h5>
            <button class="btn btn-sm btn-primary" onclick="showTaskModal()">New Task</button>
        </div>
        <div class="card-body">
            <table class="table table-sm table-striped" id="tasksTable">
                <thead><tr><th>#</th><th>Title</th><th>Done</th><th>Created</th><th></th></tr></thead>
                <tbody id="tasksTableBody"></tbody>
            </table>
        </div>
    </div>
</div>

<script>
    function loadTasks() {
        $.ajax({
            url: `${API_BASE}/tasks`, type: 'GET', contentType: 'application/json',
            data: JSON.stringify({}),
            success: function (res) {
                tasksCache = res;
                let html = res.length === 0
                    ? '<tr><td colspan="5" class="text-center text-muted">No tasks</td></tr>'
                    : res.map(t => `
                        <tr>
                            <td>${t.id}</td>
                            <td id="task-title-${t.id}"></td>
                            <td>${t.done ? 'yes' : 'no'}</td>
                            <td>${formatDate(t.created_at)}</td>
                            <td>
                                <button class="btn btn-sm btn-outline-danger" onclick="deleteTask(${t.id})">
                                    <i class="bi bi-trash"></i>
                                </button>
                            </td>
                        </tr>`).join('');
                $('#tasksTableBody').html(html);
                res.forEach(t => $(`#task-title-${t.id}`).text(t.title));
            },
            error: commonFunctionError
        });
    }
    function deleteTask(id) {
        $.ajax({
            url: `${API_BASE}/tasks`, type: 'DELETE', contentType: 'application/json',
            data: JSON.stringify({ id: id }),
            success: function () { showToast('Task deleted'); loadTasks(); },
            error: commonFunctionError
        });
    }
    $(function () { loadTasks(); });
</script>
```

### 9.4 `var/www/webroot/includes/modals/inc.task.html`

```html
<div class="modal fade" id="taskModal" tabindex="-1" aria-hidden="true">
    <div class="modal-dialog"><div class="modal-content">
        <div class="modal-header">
            <h5 class="modal-title" id="taskModalTitle">Add Task</h5>
            <button type="button" class="btn-close" data-bs-dismiss="modal"></button>
        </div>
        <div class="modal-body">
            <input type="hidden" id="taskId" value="">
            <div class="mb-3">
                <label for="taskTitle" class="form-label">Title</label>
                <input type="text" class="form-control" id="taskTitle" required>
            </div>
        </div>
        <div class="modal-footer">
            <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Cancel</button>
            <button type="button" class="btn btn-primary" onclick="saveTask()">Save</button>
        </div>
    </div></div>
</div>

<script>
    function showTaskModal() {
        $('#taskModalTitle').text('Add Task');
        $('#taskId').val(''); $('#taskTitle').val('');
        new bootstrap.Modal(document.getElementById('taskModal')).show();
    }
    function saveTask() {
        const title = $('#taskTitle').val().trim();
        if (!title) { return; }
        $.ajax({
            url: `${API_BASE}/tasks`, type: 'POST', contentType: 'application/json',
            data: JSON.stringify({ title: title }),
            success: function () { showToast('Task created'); loadTasks(); },
            error: commonFunctionError
        });
        new bootstrap.Modal(document.getElementById('taskModal')).hide();
    }
</script>
```







### 9.5 `var/www/webroot/resources.conf`

```json
[
    { "_comment": "never expose the filter", "uriRegexs": ["^/resources.conf$"],
      "actions": [ { "type": "DENY" } ] },
    { "_comment": "includes are server-side only", "uriRegexs": ["^/includes.*$"],
      "actions": [ { "type": "REDIRECT", "location": "/" } ] },
    { "_comment": "html pages: session required + templated", "uriRegexs": ["^/.*html$"],
      "requireSession": true,
      "actions": [ { "type": "PROCESS_AS", "mode": "MANTIDSLANG" }, { "type": "ACCEPT" } ] },
    { "_comment": "assets", "uriRegexs": ["^/assets.*$", "^/$"],
      "requireSession": true, "actions": [ { "type": "ACCEPT" } ] },
    { "_comment": "login is public", "uriRegexs": ["^/login.*$"],
      "requireSession": false, "actions": [ { "type": "ACCEPT" } ] },
    { "_comment": "unauthenticated -> auth proxy", "uriRegexs": ["^.*$"],
      "actions": [ { "type": "REDIRECT", "location": "/auth/retokenize/" } ] },
    { "_comment": "fallback deny", "uriRegexs": ["^.*$"],
      "actions": [ { "type": "DENY" } ] }
]
```

### 9.6 `var/www/webroot/assets/css/main.css`

```css
body { background: #f6f7f9; }
.navbar { margin-bottom: 1rem; }
```

That is a complete, runnable frontend. The only external dependency is the C++ service
providing `GET/POST/DELETE /api/v1/tasks` with `JWT_COOKIE_AUTH`.

---

## 10. Do / Don't rules

**Do**
- Keep the shell (layout, role-gated chrome) in MANTIDSLANG templates; keep data/behavior in
  vanilla JS + jQuery against the REST API.
- Compose pages from partials with `render`; keep each section/modal self-contained.
- Reference shared assets as `/assets/...` and vendor them via `npm run build-assets`.
- HTML-escape user text in the shell (`print_htmlescaped`) and in the DOM (jQuery `.text()`).
- Send and receive **JSON** on every API call; rely on the auth **cookie** for identity.
- Put `requireSession` on `*.html` and `/assets`, keep `/login` public, and add a catch-all
  redirect to the auth proxy.
- Put `$` on every JSON-path operand (`print_*`/`render`/`foreach` paths and `if` path
  operands); leave string literals and numbers un-prefixed.

**Don't**
- Don't introduce React/Vue/Angular, a bundler, or a dev server. Plain JS + vendored libs only.
- Don't serve or reference `node_modules/`; only `global/assets/` is served.
- Don't put `{{ }}` tags in `.css`/`.js`/fonts (they'd be treated as dynamic).
- Don't access `includes/` or `resources.conf` directly from the browser.
- Don't manage JWT tokens in JS; let the auth proxy + cookie handle it.
- Don't call endpoints the user's role lacks the scope for — hide them instead.
- Don't build HTML by interpolating raw user strings; use `.text()` / escaped prints.
- Don't write `if` conditions as bare paths or with `==`/`!=`; use the function-style
  operators (`IS_EQUAL`, `!IS_EQUAL`, …) with a `$` on path operands.

---

## 11. Quick reference index

### 11.1 Tag cheat sheet

| Tag | Kind | Purpose |
|-----|------|---------|
| `{{#print_json[$path]!}}` | self-close | emit raw JSON of `$path` |
| `{{#print_raw[$path]!}}` | self-close | emit raw string of `$path` (no escape) |
| `{{#print_htmlescaped[$path]!}}` | self-close | emit HTML-escaped value (safe) |
| `{{#include_raw[/path]!}}` | self-close | inject file bytes verbatim |
| `{{#render[/path]!}}` | self-close | include **and template** a partial |
| `{{#foreach [$path as v]}} … {{/foreach}}` | block | iterate a JSON array/object, binding `v` (path **must** have `$`) |
| `{{#if [IS_EQUAL($a, b)]}} … {{/if}}` | block | conditional using function-style operators (`IS_EQUAL`, `!`, `i`, `&&`, `||`, …) |

### 11.2 JSON context paths

`$session.isActive` · `$session.user` · `$session.domain` · `$session.roles` ·
`$session.scopes` · `$session.isImpersonation` · `$session.halfID` · `$session.impersonator` ·
`$client.tlsCN` · `$client.ip` · `$client.userAgent` ·
`$server.unixTime` · `$script.fullpath` · `$script.relativePath` ·
`$request.get` · `$request.post` · `$software.version` · `$software.name`

### 11.3 `resources.conf` actions

`DENY` · `REDIRECT {location, statusCode?}` · `ACCEPT {statusCode?}` ·
`PROCESS_AS {mode: MANTIDSLANG|HTMLIENGINE|RAW}` · `ADD_HEADERS` · `REPLACE_HEADERS`

### 11.4 Node.js commands (in `var/www/`)

```bash
npm install                 # first-time / new dependency
npm update [pkg] [pkg ...]  # update in-range versions (no args = update all)
npm run build-assets        # copy node_modules -> global/assets
```

### 11.5 File map (what goes where)

| File | Location | Processed by |
|------|----------|--------------|
| `index.html`, `login.html` | `var/www/webroot/` | MANTIDSLANG (if `.html` + rule) |
| `inc.section-*.html`, `inc.*.html` | `var/www/webroot/includes/` | MANTIDSLANG (via `render`) |
| `resources.conf` | `var/www/webroot/` | engine filter (never exposed) |
| `main.css` | `var/www/webroot/assets/` | static |
| `bootstrap*`, `jquery*`, `dataTables*` | `var/www/global/assets/` | static (served at `/assets`) |
| `package.json`, `node_modules/` | `var/www/` | build tooling only (not served) |

---

*End of guide.*


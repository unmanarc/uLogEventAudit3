"use strict";

const auditPages = {
    "system/resources": ["System resources", "Compare OS resource usage across Web, Alerts and Log2JSON."],
    "alerts/stats": ["Alert statistics", "Queue capacity, processing speed and filter activity."],
    "alerts/inputs": ["Inputs", "Configure TCP listeners and monitor incoming connections and JSON processing."],
    "alerts/rules": ["Rules & filters", "Define how events are evaluated. Rules run in the order shown."],
    "alerts/actions": ["Actions", "Manage command, HTTP POST and Telegram actions for alert rules."],
    "alerts/input-add": ["Add an input", "Configure a TCP listener for incoming JSON events.", "inputs"],
    "alerts/rule-add": ["Add a rule", "Choose a condition and what happens when an event matches.", "rules"],
    "alerts/action-add": ["Add an action", "Configure a command, HTTP POST or Telegram message.", "actions"],
    "log2json/events": ["Event processing", "Monitor the auditd event queue and processing throughput."],
    "log2json/eventsph": ["Events by host", "Find pending, processed and dropped events for each source host."],
    "log2json/inputs": ["Syslog+Auditd inputs", "Configure TCP listeners and monitor source connections, processed lines and invalid input."],
    "log2json/modules": ["Syslog modules", "Convert syslog messages to JSON using logger filters, message captures and templates."],
    "log2json/outputs": ["JSON outputs", "Configure JSON destinations and monitor connections, outgoing queues and delivery."]
};
let auditPending = 0;
let auditLoadFailed = false;
let auditMutationPending = false;

function auditStatus(message, state = "ready") {
    const status = document.getElementById("data-status");
    if (!status) return;
    status.textContent = message;
    status.dataset.state = state;
}

function auditRequestStarted(scope) {
    if (auditPending === 0) auditLoadFailed = false;
    auditPending++;
    auditStatus(scope === "stats" ? "Loading data…" : "Saving changes…", "loading");
    document.getElementById("refresh-data").disabled = true;
    document.querySelector(".panel-content")?.setAttribute("aria-busy", "true");
    if (scope === "control") {
        auditMutationPending = true;
        document.querySelectorAll(".editor-form button[type=submit]").forEach(button => { button.disabled = true; });
    }
}

function auditRequestFinished(failed = false) {
    auditLoadFailed ||= failed;
    auditPending = Math.max(0, auditPending - 1);
    if (auditPending) return;
    auditMutationPending = false;
    document.querySelectorAll(".editor-form button[type=submit]").forEach(button => { button.disabled = false; });
    document.getElementById("refresh-data").disabled = false;
    document.querySelector(".panel-content")?.setAttribute("aria-busy", "false");
    auditStatus(auditLoadFailed ? "Could not load data · try again" : "Updated " + new Date().toLocaleTimeString([], {hour: "2-digit", minute: "2-digit", second: "2-digit"}), auditLoadFailed ? "error" : "ready");
    auditEnhanceContent(auditLoadFailed ? "error" : "ready");
}

function auditEnhanceContent(state = "loading") {
    document.querySelectorAll(".panel-content .table").forEach(table => {
        if (!table.parentElement.classList.contains("table-responsive")) {
            const wrapper = document.createElement("div");
            wrapper.className = "table-responsive";
            wrapper.tabIndex = 0;
            wrapper.setAttribute("role", "region");
            wrapper.setAttribute("aria-label", table.getAttribute("aria-label") || "Data table; scroll horizontally for more columns");
            table.before(wrapper);
            wrapper.append(table);
        }
        table.querySelectorAll("tbody").forEach(body => {
            if (!body.querySelector("tr:not(.empty-row)")) {
                body.replaceChildren();
                const row = body.insertRow();
                row.className = "empty-row";
                const cell = row.insertCell();
                cell.colSpan = Math.max(1, table.querySelector("thead tr")?.cells.length || 1);
                cell.className = "empty-state";
                cell.textContent = state === "loading" ? "Loading data…" : state === "error" ? "Data unavailable. Use Refresh to try again." : (table.dataset.empty || "No activity to display yet.");
            }
        });
    });
    document.querySelectorAll("[data-empty]").forEach(container => {
        if (!container.querySelector(":scope > :not(.empty-state)")) {
            container.replaceChildren();
            const empty = document.createElement("div");
            empty.className = "empty-state";
            empty.textContent = state === "loading" ? "Loading data…" : state === "error" ? "Data unavailable. Use Refresh to try again." : container.dataset.empty;
            container.append(empty);
        }
    });
    auditFilterTable();
}

function auditFilterTable() {
    const search = document.getElementById("table-search");
    if (!search) return;
    const body = document.getElementById(search.dataset.target);
    if (!body) return;
    const query = search.value.trim().toLocaleLowerCase();
    const rows = Array.from(body.querySelectorAll("tr:not(.empty-row)"));
    let visible = 0;
    rows.forEach(row => {
        // Exclude row controls from the search, so "Edit" doesn't match every row.
        const text = Array.from(row.cells).filter(cell => !cell.querySelector(".btn")).map(cell => cell.textContent).join(" ");
        row.hidden = !text.toLocaleLowerCase().includes(query);
        if (!row.hidden) visible++;
    });
    const count = document.getElementById("table-count");
    if (count) count.textContent = rows.length ? `${visible} of ${rows.length} shown` : "";
    const noMatch = document.getElementById("no-search-results");
    if (noMatch) noMatch.hidden = !rows.length || visible > 0;
}

function auditConfirm(message) {
    const dialog = document.getElementById("confirm-dialog");
    if (dialog.open) return Promise.resolve(false);
    document.getElementById("confirm-message").textContent = message;
    dialog.returnValue = "cancel";
    return new Promise(resolve => {
        dialog.addEventListener("close", () => resolve(dialog.returnValue === "confirm"), {once: true});
        dialog.showModal();
    });
}

/** @brief Initialize page identity, navigation icons and interactive controls. */
function auditInitUI() {
    const key = location.pathname.replace(/^\/mods\//, "").replace(/\.html$/, "");
    const page = auditPages[key];
    const editing = key.endsWith("-add") && location.search.length > 1;
    if (page) {
        const title = editing ? page[0].replace("Add a", "Edit a").replace("Add an", "Edit an") : page[0];
        document.getElementById("apptitle").textContent = title;
        document.title = title + " · uLogEventAudit3";
        document.getElementById("page-description").textContent = page[1];
        document.getElementById("page-section").textContent = key.startsWith("alerts/") ? "Rules & alerts"
            : key.startsWith("log2json/") ? "Log to JSON" : "Workspace";
        const activeKey = page[2] ? key.split("/")[0] + "/" + page[2] : key;
        const active = document.querySelector(`.app-sidebar .nav-link[href="/mods/${activeKey}.html"]`);
        active?.classList.add("active");
        active?.setAttribute("aria-current", "page");
        // Keep the heading icon aligned with the parent page when adding or editing.
        document.getElementById("apptitle").setAttribute("data-icon", active?.querySelector("[data-icon]")?.dataset.icon || "home");
    } else {
        const home = document.querySelector(".sidebar-home");
        home.classList.add("active");
        home.setAttribute("aria-current", "page");
    }

    const toggle = document.getElementById("menu-toggle");
    const sidebar = document.getElementById("app-sidebar");
    const backdrop = document.getElementById("sidebar-backdrop");
    const mobile = matchMedia("(max-width: 900px)");
    function setNavigation(open, returnFocus = false) {
        document.body.classList.toggle("navigation-open", open);
        toggle.setAttribute("aria-expanded", String(open));
        toggle.setAttribute("aria-label", open ? "Close navigation" : "Open navigation");
        backdrop.hidden = !open;
        sidebar.inert = mobile.matches && !open;
        if (returnFocus) toggle.focus();
    }
    toggle.addEventListener("click", () => {
        const open = toggle.getAttribute("aria-expanded") !== "true";
        setNavigation(open);
        if (open) sidebar.querySelector("a").focus();
    });
    backdrop.addEventListener("click", () => setNavigation(false, true));
    mobile.addEventListener("change", () => setNavigation(false));
    document.addEventListener("keydown", event => {
        if (toggle.getAttribute("aria-expanded") !== "true") return;
        if (event.key === "Escape") setNavigation(false, true);
        if (event.key === "Tab") {
            const links = [toggle, ...sidebar.querySelectorAll("a")];
            const index = links.indexOf(document.activeElement);
            event.preventDefault();
            links[(index + (event.shiftKey ? -1 : 1) + links.length) % links.length].focus();
        }
    });
    setNavigation(false);

    const refresh = document.getElementById("refresh-data");
    refresh.hidden = !page || key.endsWith("-add") || !auditHasScope("stats");
    refresh.addEventListener("click", () => { if (!auditPending && typeof auditReady === "function") auditReady(); });
    document.getElementById("table-search")?.addEventListener("input", auditFilterTable);
    // Enter submits the visible add/save control, with native field validation.
    document.querySelector(".editor-form")?.addEventListener("submit", event => {
        event.preventDefault();
        if (auditMutationPending) return;
        const mode = editing ? "edit" : "add";
        if (key === "alerts/input-add") addInput();
        if (key === "alerts/rule-add") addRule(mode);
        if (key === "alerts/action-add") addAction(mode);
    });
    auditEnhanceContent();
}

document.addEventListener("DOMContentLoaded", auditInitUI);

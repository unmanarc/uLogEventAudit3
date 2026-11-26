"use strict";
const API_BASE = "/api/v1";

function auditHasScope(scope) {
    return Array.from(document.querySelectorAll("#audit-scopes li"))
        .some(item => item.textContent === scope);
}

/** @brief Send a scoped JSON request, renewing the CSRF value before mutations. */
function auditRequest(request, handlers = {}) {
    const method = request.method;
    const resourceHistory = method === "GET" && /^resources\/(3s|1m|60m)$/.test(request.resource);
    if (!["GET", "POST", "PUT", "DELETE"].includes(method) ||
        (!resourceHistory && !/^(alerts|log2json)\/[a-z/]+$/.test(request.resource))) {
        throw new Error("Unknown audit resource");
    }
    const requiredScope = method === "GET" ? "stats" : "control";
    if (!auditHasScope(requiredScope)) {
        (handlers.error || commonFunctionError)({status: 403, responseJSON: {message: "Permission denied"}});
        return;
    }
    const options = {
        error: commonFunctionError,
        ...handlers,
        url: API_BASE + "/" + request.resource,
        type: method,
        dataType: "json"
    };
    const track = typeof auditRequestStarted === "function";
    if (track) {
        auditRequestStarted(method);
        options.complete = (xhr, status) => {
            auditRequestFinished(!["success", "notmodified", "nocontent"].includes(status));
            if (handlers.complete) {
                handlers.complete(xhr, status);
            }
        };
    }
    if (method === "GET") {
        if (request.payload) {
            options.url += "?" + encodeURIComponent(JSON.stringify(request.payload));
        }
        return $.ajax(options);
    }
    options.contentType = "application/json";
    options.data = JSON.stringify(request.payload || {});
    // The platform auth helper can renew the cookie at any time. Bind each mutation
    // to the current session's CSRF value instead of retaining an old JWT identifier.
    // The server sends Cache-Control: no-store. Keep jQuery from adding its `_`
    // cache-busting query parameter: Mantids30 expects the whole query to be JSON.
    return $.ajax({url: API_BASE + "/session", dataType: "json", cache: true}).then(session => {
        options.headers = {"X-CSRF-Token": session.csrfToken};
        return $.ajax(options);
    }, xhr => {
        if (track) {
            auditRequestFinished(true);
        }
        (handlers.error || commonFunctionError)(xhr);
        if (handlers.complete) {
            handlers.complete(xhr, "error");
        }
    });
}

async function ajaxLoadInfo() {
    const welcome = document.getElementById("welcome");
    const displayName = typeof ufad3_accountPublicData !== "undefined"
        ? ufad3_accountPublicData?.body?.displayName : null;
    if (welcome && typeof displayName === "string" && displayName.trim()) {
        welcome.textContent = displayName;
    }
    try {
        const response = await fetch(API_BASE + "/session", {credentials: "same-origin", cache: "no-store"});
        if (!response.ok) { location.assign("/auth/retokenize/"); return; }
        if (typeof auditStatus === "function") auditStatus("Session connected");
        if (auditHasScope("stats") && typeof auditReady === "function") auditReady();
    } catch (error) {
        showToast("Could not load the audit session", "error");
        if (typeof auditStatus === "function") auditStatus("Connection failed · reload to retry", "error");
    }
}

function showToast(message, type = "success") {
    const toast = document.createElement("div");
    toast.className = "toast align-items-center text-white border-0 " + (type === "error" ? "bg-danger" : "bg-success");
    toast.setAttribute("role", "status");
    const body = document.createElement("div");
    body.className = "toast-body";
    const text = document.createElement("span");
    text.textContent = message;
    body.appendChild(text);
    const close = document.createElement("button");
    close.type = "button";
    close.className = "btn-close btn-close-white";
    close.setAttribute("aria-label", "Dismiss notification");
    close.setAttribute("data-bs-dismiss", "toast");
    body.appendChild(close);
    toast.appendChild(body);
    document.querySelector(".toast-container").appendChild(toast);
    toast.addEventListener("hidden.bs.toast", () => toast.remove());
    new bootstrap.Toast(toast).show();
}

function commonFunctionError(xhr) {
    if (xhr.status === 401 || xhr.status === 403) {
        showToast("Session expired or permission denied", "error");
        return;
    }
    showToast(xhr.responseJSON?.message || "Audit request failed", "error");
}

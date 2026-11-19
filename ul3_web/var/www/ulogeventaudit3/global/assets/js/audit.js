"use strict";
const API_BASE = "/api/v1";

function auditHasScope(scope) {
    return Array.from(document.querySelectorAll("#audit-scopes li"))
        .some(item => item.textContent === scope);
}

// JSON GET arguments use Mantids30's encoded query convention. Identity stays in cookies.
function auditRequest(request, handlers = {}) {
    const method = request.method;
    if (!["GET", "POST", "PUT", "DELETE"].includes(method) ||
        !/^(alerts|log2json)\/[a-z/]+$/.test(request.resource)) {
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
    if (method === "GET") {
        if (request.payload) options.url += "?" + encodeURIComponent(JSON.stringify(request.payload));
        return $.ajax(options);
    }
    options.contentType = "application/json";
    options.data = JSON.stringify(request.payload || {});
    // The platform auth helper can renew the cookie at any time. Bind each mutation
    // to the current session's CSRF value instead of retaining an old JWT identifier.
    return $.ajax({url: API_BASE + "/session", dataType: "json", cache: false}).then(session => {
        options.headers = {"X-CSRF-Token": session.csrfToken};
        return $.ajax(options);
    }, handlers.error || commonFunctionError);
}

async function ajaxLoadInfo() {
    try {
        const response = await fetch(API_BASE + "/session", {credentials: "same-origin", cache: "no-store"});
        if (!response.ok) { location.assign("/auth/retokenize/"); return; }
        if (auditHasScope("stats") && typeof auditReady === "function") auditReady();
    } catch (error) {
        showToast("Could not load the audit session", "error");
    }
}

function showToast(message, type = "success") {
    const toast = document.createElement("div");
    toast.className = "toast align-items-center text-white border-0 " + (type === "error" ? "bg-danger" : "bg-success");
    toast.setAttribute("role", "status");
    const body = document.createElement("div");
    body.className = "toast-body";
    body.textContent = message;
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

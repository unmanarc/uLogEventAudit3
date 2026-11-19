"use strict";
const auditRoutes = {
  "ALERTS:stats.ReadRunningRules": [
    "GET",
    "alerts/rules"
  ],
  "ALERTS:stats.ReadRunningRule": [
    "GET",
    "alerts/rules/item"
  ],
  "ALERTS:stats.AreRulesModified": [
    "GET",
    "alerts/rules/modified"
  ],
  "ALERTS:stats.ReadRulesFileConfig": [
    "GET",
    "alerts/rules/config"
  ],
  "ALERTS:control.WriteRulesFileConfig": [
    "PUT",
    "alerts/rules/config"
  ],
  "ALERTS:control.AddRule": [
    "POST",
    "alerts/rules"
  ],
  "ALERTS:control.EditRule": [
    "PUT",
    "alerts/rules"
  ],
  "ALERTS:control.RemoveRule": [
    "DELETE",
    "alerts/rules"
  ],
  "ALERTS:control.SaveRunningRules": [
    "POST",
    "alerts/rules/save"
  ],
  "ALERTS:control.RulesReload": [
    "POST",
    "alerts/rules/reload"
  ],
  "ALERTS:stats.ReadRunningActions": [
    "GET",
    "alerts/actions"
  ],
  "ALERTS:stats.ReadRunningAction": [
    "GET",
    "alerts/actions/item"
  ],
  "ALERTS:stats.AreActionsModified": [
    "GET",
    "alerts/actions/modified"
  ],
  "ALERTS:stats.ReadActionsFileConfig": [
    "GET",
    "alerts/actions/config"
  ],
  "ALERTS:control.WriteActionsFileConfig": [
    "PUT",
    "alerts/actions/config"
  ],
  "ALERTS:control.AddAction": [
    "POST",
    "alerts/actions"
  ],
  "ALERTS:control.EditAction": [
    "PUT",
    "alerts/actions"
  ],
  "ALERTS:control.RemoveAction": [
    "DELETE",
    "alerts/actions"
  ],
  "ALERTS:control.SaveRunningActions": [
    "POST",
    "alerts/actions/save"
  ],
  "ALERTS:control.RulesActions": [
    "POST",
    "alerts/actions/reload"
  ],
  "ALERTS:control.RuleUp": [
    "POST",
    "alerts/rules/up"
  ],
  "ALERTS:control.RuleDown": [
    "POST",
    "alerts/rules/down"
  ],
  "ALERTS:stats.ReadRunningInputs": [
    "GET",
    "alerts/inputs"
  ],
  "ALERTS:stats.ReadInputsFileConfig": [
    "GET",
    "alerts/inputs/config"
  ],
  "ALERTS:control.AddInputToFileConfig": [
    "POST",
    "alerts/inputs/config"
  ],
  "ALERTS:control.RemInputFromFileConfig": [
    "DELETE",
    "alerts/inputs/config"
  ],
  "ALERTS:stats.SystemStats": [
    "GET",
    "alerts/stats"
  ],
  "ALERTS:control.ReloadApp": [
    "POST",
    "alerts/reload"
  ],
  "LOG2JSON:stats.EventsPerHost": [
    "GET",
    "log2json/hosts"
  ],
  "LOG2JSON:stats.OutputsDistributionThreads": [
    "GET",
    "log2json/distribution"
  ],
  "LOG2JSON:stats.Outputs": [
    "GET",
    "log2json/outputs"
  ],
  "LOG2JSON:stats.Inputs": [
    "GET",
    "log2json/inputs"
  ]
};
function auditRequest(request, handlers) {
    const route = auditRoutes[request.target + ":" + request.remoteMethod];
    if (!route) throw new Error("Unknown audit resource");
    const options = {
        ...handlers,
        url: "/api/v1/" + route[1],
        type: route[0],
        dataType: "json",
        headers: {"X-CSRF-Token": window.csrfToken || ""}
    };
    if (route[0] === "GET") {
        if (request.payload) options.url += "?" + encodeURIComponent(JSON.stringify(request.payload));
    } else {
        options.contentType = "application/json";
        options.data = JSON.stringify(request.payload || {});
    }
    return $.ajax(options);
}
async function ajaxLoadInfo() {
    try {
        const response = await fetch("/api/v1/session", {credentials: "same-origin", cache: "no-store"});
        if (!response.ok) { location.assign("/login"); return; }
        const session = await response.json();
        window.csrfToken = session.csrfToken || "";
        document.getElementById("welcome").textContent = session.user || "Audit console";
        document.getElementById("version").textContent = session.version;
        scheduleRefresh(session.maxAge);
        if (typeof csrfReady === "function") csrfReady();
    } catch (error) { console.error(error); }
}
async function logout() {
    const response = await fetch("/auth/api/v1/logout", {method:"POST", credentials:"same-origin",
        headers:{"X-CSRF-Token":window.csrfToken || ""}});
    if (response.ok) location.assign("/login");
    else alert("Could not close the authentication session.");
}

function scheduleRefresh(maxAge) {
    setTimeout(async () => {
        try {
            const refresh = await fetch("/auth/api/v1/refreshAccessToken", {method:"POST",credentials:"same-origin"});
            if (!refresh.ok) { location.assign("/login"); return; }
            const response = await fetch("/api/v1/session", {credentials:"same-origin", cache:"no-store"});
            if (!response.ok) { location.assign("/login"); return; }
            const session = await response.json();
            window.csrfToken = session.csrfToken;
            scheduleRefresh(session.maxAge);
        } catch (error) { location.assign("/login"); }
    }, Math.max(1, Number(maxAge)-10)*1000);
}

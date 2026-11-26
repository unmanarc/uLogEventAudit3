"use strict";

let endpointsLoaded = false;
let endpointsBusy = false;
let endpoints = [];
let editingEndpoint = null;
const endpointKind = document.getElementById("endpoint-config").dataset.kind;
const endpointInputs = endpointKind === "inputs";
const endpointFields = endpointInputs ? ["Description", "ListenAddr", "ListenPort"] :
    ["Description", "Host", "Port", "TransmissionMode", "TimeoutSecs", "ReconnectSleepTimeInSecs", "MaxQueuedItems", "QueuePushTimeoutInMS"];
const endpointNumbers = new Set(["ListenPort", "Port", "TransmissionMode", "TimeoutSecs", "ReconnectSleepTimeInSecs", "MaxQueuedItems", "QueuePushTimeoutInMS"]);
const endpointDialog = document.getElementById("endpoint-dialog");
const endpointForm = document.getElementById("endpoint-form");

/** @brief Keep mutation controls consistent with loading and scope checks. */
function updateEndpointControls() {
    const allowed = auditHasScope("control");
    const disabled = !endpointsLoaded || endpointsBusy || !allowed;
    document.getElementById("endpoint-reload").hidden = !allowed;
    document.getElementById("endpoint-reload").disabled = disabled;
    document.getElementById("endpoint-add").hidden = !allowed;
    document.getElementById("endpoint-add").disabled = disabled;
    document.querySelectorAll("#endpoints-list button, #endpoint-form input, #endpoint-form select, #endpoint-form button")
        .forEach(control => { control.disabled = disabled; });
}

/** @brief Render the persisted snapshot using text nodes for backend values. */
function renderEndpoints() {
    const body = document.getElementById("endpoints-list");
    body.replaceChildren();
    endpoints.forEach((definition, index) => {
        const row = body.insertRow();
        for (const key of endpointInputs ? ["Description", "ListenAddr", "ListenPort"] : ["Description", "Host", "Port"]) {
            const cell = row.insertCell();
            cell.textContent = definition[key];
            if (key !== "Description") {
                cell.className = "code-input";
            }
        }
        const controls = row.insertCell();
        controls.className = "text-end text-nowrap";
        if (auditHasScope("control")) {
            for (const [title, style, handler] of [
                ["Edit", "btn-outline-secondary", () => { openEndpointEditor(index); }],
                ["Remove", "btn-outline-danger", () => { removeEndpoint(index); }]
            ]) {
                const button = document.createElement("button");
                button.type = "button";
                button.className = `btn btn-sm ${style}`;
                button.textContent = title;
                button.setAttribute("data-icon", title === "Edit" ? "edit" : "remove");
                button.setAttribute("aria-label", `${title} endpoint ${definition.Description}`);
                button.addEventListener("click", handler);
                controls.appendChild(button);
            }
        }
    });
    auditEnhanceContent("ready");
    updateEndpointControls();
}

/** @brief Open an isolated draft; cancelling leaves the saved list intact. */
function openEndpointEditor(index = null) {
    if (!endpointsLoaded || endpointsBusy || !auditHasScope("control") || endpointDialog.open) {
        return;
    }
    editingEndpoint = index;
    const definition = index === null ? (endpointInputs ? {
        Description: "Syslog TCP input", ListenAddr: "0.0.0.0", ListenPort: 10514
    } : {
        Description: "JSON TCP output", Host: "127.0.0.1", Port: 9999, TransmissionMode: 0,
        TimeoutSecs: 10, ReconnectSleepTimeInSecs: 3, MaxQueuedItems: 500000, QueuePushTimeoutInMS: 0
    }) : endpoints[index];
    for (const key of endpointFields) {
        document.getElementById(`endpoint-${key}`).value = definition[key];
    }
    document.getElementById("endpoint-dialog-title").textContent = `${index === null ? "New" : "Edit"} ${endpointInputs ? "input" : "output"}`;
    document.getElementById("endpoint-error").textContent = "";
    endpointDialog.showModal();
}

/** @brief Save all endpoints atomically, committing the UI only after success. */
function saveEndpoints(definitions) {
    if (!endpointsLoaded || endpointsBusy || !auditHasScope("control")) {
        return;
    }
    endpointsBusy = true;
    updateEndpointControls();
    auditRequest({method: "PUT", resource: `log2json/${endpointKind}/config`, payload: {[endpointKind]: definitions}}, {
        success: result => {
            const message = result.ok ? (result.restartRequired ? "Configuration saved. Restart the service to apply changes." : "Configuration saved. No restart needed.") : (result.message || "Could not save endpoints");
            document.getElementById("endpoints-message").textContent = message;
            if (result.ok) {
                endpoints = result[endpointKind];
                document.getElementById("endpoint-restart").hidden = !result.restartRequired;
                endpointDialog.close();
                renderEndpoints();
            } else {
                document.getElementById("endpoint-error").textContent = message;
            }
            showToast(message, result.ok ? "success" : "error");
        },
        error: xhr => {
            const message = xhr.responseJSON?.message || "Could not save endpoints. Try again.";
            document.getElementById("endpoint-error").textContent = message;
            document.getElementById("endpoints-message").textContent = message;
            commonFunctionError(xhr);
        },
        complete: () => { endpointsBusy = false; updateEndpointControls(); }
    });
}

/** @brief Confirm removal and save the remaining definitions for a service restart. */
async function removeEndpoint(index) {
    if (!endpointsLoaded || endpointsBusy || !auditHasScope("control")) {
        return;
    }
    // Preserve the selected object if a refresh completes while confirming.
    const definition = endpoints[index];
    if (!await auditConfirm(`Remove endpoint "${definition.Description}"? Restart the service afterwards to apply the change.`)) {
        return;
    }
    if (!endpoints.includes(definition)) {
        return;
    }
    saveEndpoints(endpoints.filter(endpoint => endpoint !== definition));
}

/** @brief Refresh saved endpoints and running activity without overwriting an open draft. */
function auditReady() {
    if (endpointsBusy || endpointDialog.open) {
        return;
    }
    loadEndpointActivity();
    endpointsBusy = true;
    updateEndpointControls();
    auditRequest({method: "GET", resource: `log2json/${endpointKind}/config`}, {
        success: result => {
            if (!result.ok) {
                showToast(result.message || "Could not load endpoints", "error");
                return;
            }
            endpoints = result[endpointKind];
            document.getElementById("endpoint-restart").hidden = !result.restartRequired;
            endpointsLoaded = true;
            renderEndpoints();
            document.getElementById("endpoints-message").textContent = "Endpoints loaded.";
        },
        error: commonFunctionError,
        complete: () => { endpointsBusy = false; updateEndpointControls(); }
    });
}

document.getElementById("endpoint-add").addEventListener("click", () => { openEndpointEditor(); });
document.getElementById("endpoint-cancel").addEventListener("click", () => { endpointDialog.close(); });
endpointDialog.addEventListener("cancel", event => {
    if (endpointsBusy) {
        event.preventDefault();
    }
});
endpointForm.addEventListener("submit", event => {
    event.preventDefault();
    if (!endpointsLoaded || endpointsBusy || !auditHasScope("control") || !endpointForm.reportValidity()) {
        return;
    }
    // Keep identities stable across edits and retain any future optional fields.
    const definition = editingEndpoint === null ? {id: crypto.randomUUID()} : {...endpoints[editingEndpoint]};
    for (const key of endpointFields) {
        const value = document.getElementById(`endpoint-${key}`).value;
        definition[key] = endpointNumbers.has(key) ? Number(value) : value;
    }
    const updated = endpoints.slice();
    if (editingEndpoint === null) {
        updated.push(definition);
    } else {
        updated[editingEndpoint] = definition;
    }
    document.getElementById("endpoint-error").textContent = "";
    saveEndpoints(updated);
});

/** @brief Request the service-manager restart after confirming a short processing interruption. */
async function restartLog2JSON() {
    if (endpointsBusy || !auditHasScope("control") ||
        !await auditConfirm("Restart Log to JSON to apply saved inputs and outputs? Processing will briefly stop.")) {
        return;
    }
    endpointsBusy = true;
    updateEndpointControls();
    document.getElementById("endpoint-reload").disabled = true;
    let restarting = false;
    auditRequest({method: "POST", resource: "log2json/reload", payload: {}}, {
        success: result => {
            if (result === true) {
                restarting = true;
                document.getElementById("endpoints-message").textContent = "Restarting service. Please wait…";
                // Allow the development unit's restart delay before reloading.
                setTimeout(() => { location.reload(); }, 10000);
            } else {
                showToast("Could not restart service", "error");
            }
        },
        error: commonFunctionError,
        complete: () => {
            endpointsBusy = restarting;
            updateEndpointControls();
        }
    });
}
document.getElementById("endpoint-reload").addEventListener("click", restartLog2JSON);

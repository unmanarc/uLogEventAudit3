"use strict";
let argn = 1;
let storedActionType = "";
let storedBotToken = false;
let storedBearerToken = false;

/** Initialize the action editor, including legacy command definitions. */
function auditReady() {
    updateActionType();
    const query = window.location.search.substring(1);
    if (query) {
        try {
            displayAction(decodeURIComponent(escape(atob(decodeURIComponent(query)))));
        } catch (error) {
            showToast("Invalid action identifier.", "error");
        }
    }
}

/** Enable only fields relevant to the selected delivery type. */
function updateActionType() {
    const type = document.getElementById("actionType").value;
    document.querySelectorAll("[data-action-types]").forEach(section => {
        const active = section.dataset.actionTypes.split(" ").includes(type);
        section.hidden = !active;
        section.querySelectorAll("input, textarea, select, button").forEach(field => {
            field.disabled = !active;
        });
    });
    document.getElementById("botToken").required = type === "telegram" && !(storedActionType === type && storedBotToken);
    document.getElementById("remove-argument").disabled = type !== "command" || argn <= 1;
}

/** Populate public fields; stored credentials are deliberately never returned. */
function displayAction(actionId) {
    auditRequest({method: "GET", resource: "alerts/actions/item", payload: {name: actionId}}, {
        success: function (result) {
            if (!result) {
                showToast("Unable to load action.", "error");
                return;
            }
            storedActionType = result.type || "command";
            storedBotToken = Boolean(result.hasBotToken);
            storedBearerToken = Boolean(result.hasBearerToken);
            document.getElementById("actionType").value = storedActionType;
            const fields = {actionName: "name", actionDescription: "description", actionExecPath: "path",
                stdinTemplate: "stdinTemplate", url: "url", contentType: "contentType", bodyTemplate: "bodyTemplate",
                chatId: "chatId", messageTemplate: "messageTemplate", parseMode: "parseMode", timeoutSeconds: "timeoutSeconds"};
            Object.entries(fields).forEach(([id, key]) => {
                if (result[key] !== undefined) {
                    document.getElementById(id).value = result[key];
                }
            });
            document.getElementById("actionName").readOnly = true;
            document.getElementById("appendNewLineToEachArgv").checked = Boolean(result.appendNewLineToEachArgv);
            document.getElementById("disableNotification").checked = Boolean(result.disableNotification);
            document.getElementById("botToken").value = "";
            document.getElementById("bearerToken").value = "";
            document.getElementById("botToken").placeholder = storedBotToken ? "Token configured; leave blank to keep" : "Enter bot token";
            document.getElementById("bearerToken").placeholder = storedBearerToken ? "Token configured; leave blank to keep" : "Optional bearer token (HTTPS only)";
            while (argn > 1) {
                remLastArgument();
            }
            const args = result.args || [];
            document.getElementById("inputArg0").value = args[0] || "";
            for (let i = 1; i < args.length; i++) {
                addNewArgument();
                document.getElementById(`inputArg${i}`).value = args[i];
            }
            document.getElementById("EditButton").style.display = "block";
            document.getElementById("EditButton").style.visibility = "visible";
            document.getElementById("AddButton").style.display = "none";
            updateActionType();
        },
        error: commonFunctionError
    });
}

/** Build an explicit per-type payload; omitted credentials retain server-side values. */
function actionPayload() {
    const value = id => document.getElementById(id).value;
    const payload = {name: value("actionName"), description: value("actionDescription"), type: value("actionType")};
    if (payload.type === "command") {
        Object.assign(payload, {path: value("actionExecPath"), stdinTemplate: value("stdinTemplate"),
            appendNewLineToEachArgv: document.getElementById("appendNewLineToEachArgv").checked, args: []});
        for (let i = 0; i < argn; i++) {
            payload.args.push(value(`inputArg${i}`));
        }
        if (!payload.args[0]) {
            payload.args[0] = payload.path;
        }
    } else {
        payload.timeoutSeconds = Number(value("timeoutSeconds"));
        if (payload.type === "http_post") {
            Object.assign(payload, {url: value("url"), contentType: value("contentType"), bodyTemplate: value("bodyTemplate")});
            if (document.getElementById("clearBearerToken").checked) {
                payload.bearerToken = "";
            } else if (value("bearerToken")) {
                payload.bearerToken = value("bearerToken");
            }
        } else {
            Object.assign(payload, {chatId: value("chatId"), messageTemplate: value("messageTemplate"),
                parseMode: value("parseMode"), disableNotification: document.getElementById("disableNotification").checked});
            if (value("botToken")) {
                payload.botToken = value("botToken");
            }
        }
    }
    return payload;
}

/** Validate the form and submit a running action definition. */
function addAction(editMode) {
    if (!document.getElementById("editor-form").reportValidity()) {
        return false;
    }
    auditRequest({method: editMode === "add" ? "POST" : "PUT", resource: "alerts/actions", payload: actionPayload()}, {
        success: function (result) {
            if (result === true) {
                document.getElementById("botToken").value = "";
                document.getElementById("bearerToken").value = "";
                document.location = "/mods/alerts/actions.html";
            } else {
                showToast("Action rejected. Check the destination, credentials and template fields.", "error");
            }
        },
        error: commonFunctionError
    });
    return true;
}

/** Append a positional argument without interpreting its contents as HTML. */
function addNewArgument() {
    const group = document.createElement("div");
    group.className = "form-group";
    group.id = `formArg${argn}`;
    const label = document.createElement("label");
    label.htmlFor = `inputArg${argn}`;
    label.textContent = `Argument ${argn}`;
    const input = document.createElement("input");
    input.type = "text";
    input.className = "form-control code-input";
    input.id = `inputArg${argn}`;
    group.append(label, input);
    document.getElementById("arguments").append(group);
    argn++;
    document.getElementById("remove-argument").disabled = false;
}

/** Remove the last optional positional argument. */
function remLastArgument() {
    if (argn > 1) {
        argn--;
        document.getElementById(`formArg${argn}`).remove();
        document.getElementById("remove-argument").disabled = argn <= 1;
    }
}

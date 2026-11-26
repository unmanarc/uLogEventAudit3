"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const root = path.resolve(__dirname, "..");
const html = fs.readFileSync(path.join(root, "ul3_web/var/www/ulogeventaudit3/webroot/mods/alerts/action-add.html"), "utf8");
const elements = new Map();
/** Build a small DOM sufficient to exercise form behavior and payloads. */
function element(id) {
    if (!elements.has(id)) {
        elements.set(id, {id, value: "", checked: false, disabled: false, style: {}, dataset: {},
            append() {}, remove() { elements.delete(id); }, reportValidity() { return true; }});
    }
    return elements.get(id);
}
for (const match of html.matchAll(/id="([^"]+)"/g)) {
    element(match[1]);
}
const sectionFields = {
    command: ["actionExecPath", "stdinTemplate", "appendNewLineToEachArgv", "inputArg0", "remove-argument"],
    http_post: ["url", "contentType", "bearerToken", "clearBearerToken", "bodyTemplate"],
    telegram: ["botToken", "chatId", "messageTemplate", "parseMode", "disableNotification"],
    "http_post telegram": ["timeoutSeconds"]
};
const sections = Object.entries(sectionFields).map(([types, ids]) => ({
    dataset: {actionTypes: types}, querySelectorAll: () => ids.map(element)
}));
let pending;
const context = vm.createContext({
    document: {getElementById: element, querySelectorAll: () => sections,
        createElement() {
            const value = element("temporary");
            elements.delete("temporary");
            Object.defineProperty(value, "id", {set(id) { elements.set(id, value); }});
            return value;
        }},
    window: {location: {search: ""}},
    auditRequest: (request, callbacks) => { pending = {request, callbacks}; },
    commonFunctionError() {}, showToast() {}
});
vm.runInContext(fs.readFileSync(path.join(root, "ul3_web/var/www/ulogeventaudit3/global/assets/js/actions.js"), "utf8"), context);
element("actionType").value = "command";
element("timeoutSeconds").value = "15";
element("contentType").value = "application/json";
vm.runInContext("auditReady()", context);
assert.equal(sections[0].hidden, false);
assert.equal(element("botToken").disabled, true);
assert.equal(element("actionExecPath").disabled, false);
element("actionType").value = "telegram";
vm.runInContext("updateActionType()", context);
assert.equal(element("actionExecPath").disabled, true);
assert.equal(element("botToken").disabled, false);
assert.equal(element("botToken").required, true);
assert.equal(element("timeoutSeconds").disabled, false);

vm.runInContext('displayAction("notify")', context);
assert.equal(pending.request.method, "ReadRunningAction");
pending.callbacks.success({name: "notify", type: "telegram", hasBotToken: true,
    chatId: "@security", messageTemplate: "Text <script> stays text", parseMode: "", timeoutSeconds: 15});
assert.equal(element("botToken").value, "");
assert.equal(element("botToken").required, false);
assert.equal(element("messageTemplate").value, "Text <script> stays text");
assert.equal(element("actionName").readOnly, true);
vm.runInContext('addAction("edit")', context);
let payload = JSON.parse(JSON.stringify(pending.request.payload));
assert.equal(pending.request.method, "EditAction");
assert.equal(payload.type, "telegram");
assert.equal(payload.chatId, "@security");
assert.equal(payload.botToken, undefined);
assert.equal(payload.path, undefined);
assert.equal(payload.args, undefined);
element("botToken").value = "123:new_token";
vm.runInContext('addAction("edit")', context);
assert.equal(pending.request.payload.botToken, "123:new_token");

// Switching away from a stored credential must not treat it as a new type's token.
element("actionType").value = "http_post";
element("url").value = "https://example.com/hook";
element("bodyTemplate").value = "{{#print_json[$]!}}";
vm.runInContext("updateActionType(); addAction('edit')", context);
payload = JSON.parse(JSON.stringify(pending.request.payload));
assert.equal(payload.botToken, undefined);
assert.equal(payload.messageTemplate, undefined);
assert.equal(payload.bearerToken, undefined);
assert.equal(payload.contentType, "application/json");
element("clearBearerToken").checked = true;
vm.runInContext("addAction('edit')", context);
assert.equal(pending.request.payload.bearerToken, "");

// Legacy program definitions remain editable and preserve their argument order.
vm.runInContext('displayAction("old")', context);
pending.callbacks.success({name: "old", path: "/usr/bin/example", args: ["example", "%-.INFO.host%"], appendNewLineToEachArgv: true});
assert.equal(element("actionType").value, "command");
assert.equal(element("inputArg1").value, "%-.INFO.host%");
assert.equal(element("appendNewLineToEachArgv").checked, true);
element("stdinTemplate").value = "{{#print_json[$]!}}\n";
vm.runInContext("addAction('edit')", context);
payload = JSON.parse(JSON.stringify(pending.request.payload));
assert.deepEqual(payload.args, ["example", "%-.INFO.host%"]);
assert.equal(payload.stdinTemplate, "{{#print_json[$]!}}\n");
assert.equal(payload.botToken, undefined);
assert.equal(payload.bearerToken, undefined);
assert.equal(payload.timeoutSeconds, undefined);

// Rendered HTML help/default examples must not become server-side template commands.
assert.equal(html.includes("{{#print_"), false);
assert.match(html, /type="password"[^>]*id="botToken"/);
console.log("PASS: action editor types, legacy arguments, templates and secret-preserving edits");

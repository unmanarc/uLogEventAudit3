"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const html = fs.readFileSync(path.join(__dirname, "../ul3_web/var/www/ulogeventaudit3/webroot/mods/alerts/rule-add.html"), "utf8");
const elements = new Map();
/** @brief Provide the form controls used by the rule editor. */
function element(id) {
    if (!elements.has(id)) {
        elements.set(id, {value: "", checked: false, disabled: false, style: {}, textContent: ""});
    }
    return elements.get(id);
}
const select = element("actions");
select.options = [];
let selectedValue = "";
// A native single-select rejects values that are absent from its current options.
Object.defineProperty(select, "value", {
    get() { return selectedValue; },
    set(value) { selectedValue = select.options.some(option => option.value === value) ? value : ""; }
});
select.dispatchEvent = () => {};
select.replaceChildren = (...options) => { select.options = options; selectedValue = ""; };
let pending;
const context = vm.createContext({
    document: {getElementById: element, createElement: () => ({value: "", textContent: ""})},
    window: {location: {search: ""}},
    Event: class { constructor(type) { this.type = type; } },
    $: selector => ({val: () => element(selector.slice(1)).value}),
    auditRequest(request, callbacks) { pending = {request, callbacks}; },
    commonFunctionError() {}, showToast() {}
});
for (const script of html.matchAll(/<script>([\s\S]*?)<\/script>/g)) {
    vm.runInContext(script[1], context);
}
const actions = [
    {name: "notify", description: "Security team"},
    {name: "archive", description: "Store JSON events"},
    {name: "Alért <ops>", description: "On-call notification"}
];
vm.runInContext("loadActions()", context);
pending.callbacks.success(actions);
assert.equal(select.value, "");
assert.equal(select.options.length, 4);
assert.equal(select.options.at(-1).textContent, "Alért <ops>: On-call notification");
// Reloading the options preserves an existing selection.
select.value = "notify";
vm.runInContext("loadActions()", context);
pending.callbacks.success(actions);
assert.equal(select.value, "notify");
vm.runInContext("displayRule(2)", context);
pending.callbacks.success({name: "Existing rule", action: "EXEC", actionId: "archive", filter: "true"});
assert.equal(select.value, "archive");
element("editor-form").reportValidity = () => true;
vm.runInContext('addRule("edit")', context);
assert.equal(pending.request.method, "EditRule");
assert.equal(pending.request.payload.rule.actionId, "archive");
// Stop-only rules disable the action control and omit the action from the payload.
element("bABORT").checked = true;
element("bEXEC").checked = false;
vm.runInContext('checkActions(); addRule("edit")', context);
assert.equal(select.disabled, true);
assert.equal(pending.request.payload.rule.actionId, undefined);
element("bABORT").checked = false;
element("bEXEC").checked = true;
vm.runInContext("checkActions()", context);
assert.equal(select.disabled, false);
assert.equal(select.value, "archive");
// An empty action list cannot acquire a selection without an explicit choice.
vm.runInContext("loadActions()", context);
pending.callbacks.success([]);
assert.equal(select.value, "");
assert.equal(select.options.length, 1);
console.log("PASS: action options, Unicode labels, selection preservation, edited rule payloads and stop-only behavior");

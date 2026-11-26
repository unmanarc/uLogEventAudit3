"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

/** @brief Minimal persistent DOM elements for exercising the dashboard renderers and events. */
class Element {
    /** @brief Initialize an element without browser dependencies. */
    constructor(tag = "div") {
        this.tag = tag;
        this.children = [];
        this.attributes = {};
        this.dataset = {};
        this.listeners = {};
        this.textContent = "";
    }
    /** @brief Retain rendered attributes for assertions. */
    setAttribute(name, value) { this.attributes[name] = String(value); }
    /** @brief Append child nodes. */
    append(...children) { this.children.push(...children); }
    /** @brief Replace chart content between refreshes. */
    replaceChildren(...children) { this.children = children; }
    /** @brief Retain control handlers for simulated user interaction. */
    addEventListener(name, callback) { this.listeners[name] = callback; }
    /** @brief Return a predictable pointer-to-chart coordinate mapping. */
    getBoundingClientRect() { return {left: 0, width: 660}; }
}

const elements = new Map(), requests = [], timers = new Map(), windowEvents = {}, documentEvents = {};
let timerId = 0, scopes = ["stats"];
/** @brief Preserve element identity across render calls. */
function element(id) {
    if (!elements.has(id)) {
        elements.set(id, new Element());
    }
    return elements.get(id);
}
const context = vm.createContext({
    document: {
        hidden: false, getElementById: element,
        createElement: tag => new Element(tag), createElementNS: (namespace, tag) => new Element(tag),
        querySelectorAll: () => scopes.map(textContent => ({textContent})),
        addEventListener: (name, callback) => { documentEvents[name] = callback; }
    },
    window: {addEventListener: (name, callback) => { windowEvents[name] = callback; }},
    setTimeout: (callback, delay) => { timers.set(++timerId, {callback, delay}); return timerId; },
    clearTimeout: id => timers.delete(id),
    $: {ajax: options => { requests.push(options); return options; }},
    commonFunctionError: () => {}
});
const assets = path.resolve(__dirname, "../ul3_web/var/www/ulogeventaudit3/global/assets/js");
vm.runInContext(fs.readFileSync(path.join(assets, "audit.js"), "utf8"), context);
vm.runInContext(fs.readFileSync(path.join(assets, "resources.js"), "utf8"), context);
const run = source => vm.runInContext(source, context);

assert.equal(run('resourceValue({processIO: {readBytesPerSecond: 0}}, "processIO.readBytesPerSecond")'), 0);
assert.equal(run('resourceValue({processIO: null}, "processIO.readBytesPerSecond")'), null);
assert.equal(run('resourceNumber(null)'), "Unavailable");
assert.equal(run('resourceUptime(0)'), "0d 0h 0m");
run("auditReady(); auditReady();");
assert.equal(requests.length, 3, "Overlapping polling cycles were sent");
for (const [index, target] of ["WEB", "ALERTS", "LOG2JSON"].entries()) {
    assert.equal(requests[index].url, "/api/v1/remote.stats");
    assert.equal(requests[index].type, "POST");
    assert.deepEqual(JSON.parse(requests[index].data), {target, method: "ResourceMonitor", payload: {resolution: "3s"}});
}
assert.throws(() => run('auditRequest({method: "GET", resource: "resources/3s"})'));

/** @brief Complete a polling cycle in a different order from dispatch, including partial failures. */
function finishCycle(start, response) {
    for (const offset of [2, 0, 1]) {
        const request = requests[start + offset];
        const component = JSON.parse(request.data).target.toLowerCase();
        const history = response.components[component];
        if (history?.available) {
            request.success(history);
        } else {
            request.error({status: 503});
        }
        request.complete({}, history?.available ? "success" : "error");
        if (offset !== 1) {
            assert.equal(run("resourceDashboard.pending"), true, "Polling resumed before every component completed");
        }
    }
}

const now = Date.now();
const response = {resolution: "3s", components: {
    web: {available: true, intervalSeconds: 3, capacity: 300, samples: [
        {timestamp: now - 9000, processCpuPercent: 0, residentMemoryBytes: 1048576},
        {timestamp: now - 6000, processCpuPercent: null, residentMemoryBytes: null},
        {timestamp: now, processId: 123, processCpuPercent: 5, residentMemoryBytes: 2097152, threadCount: 4, handleCount: 8,
            processUptimeSeconds: 0, systemMemory: {totalBytes: 1073741824, usedPercent: 25},
            processIO: {readBytesPerSecond: 1024, writeBytesPerSecond: 0}}
    ]},
    alerts: {available: false, error: "service_disconnected"},
    log2json: {available: true, intervalSeconds: 3, capacity: 300,
        samples: [{timestamp: now - 3000, processCpuPercent: 10}, {timestamp: now, processCpuPercent: 11}]}
}};
for (const history of Object.values(response.components)) {
    if (history.samples) {
        history.latest = history.samples.at(-1);
    }
}
finishCycle(0, response);
assert.equal(element("resource-status-alerts").textContent, "Disconnected");
assert.match(element("resource-message").textContent, /Alerts/);
assert.match(element("resource-readout-memory").textContent, /Web: 2 MiB/);
assert.match(element("resource-readout-io").textContent, /Web read: 1 KiB\/s/);
assert.match(element("resource-readout-io").textContent, /Web write: 0 KiB\/s/);
assert.match(element("resource-readout-storage").textContent, /Unavailable/);
const cpu = element("resource-chart-cpu");
const cpuPaths = cpu.children.filter(child => child.tag === "path");
assert.equal(cpuPaths.length, 3, "Chart did not combine all components");
assert.equal((cpuPaths[0].attributes.d.match(/M/g) || []).length, 2, "Null samples were bridged");
assert.equal(cpuPaths[1].attributes.d, "", "Disconnected service was plotted");
assert.equal((cpuPaths[2].attributes.d.match(/L/g) || []).length, 1, "Actual timestamp series was not connected");
assert.equal([...timers.values()][0].delay, 3000);
cpu.onpointermove({clientX: 644});
assert.match(element("resource-readout-cpu").textContent, /Web: 5 %/);
cpu.onpointerleave();
for (const metric of ["cpu", "memory", "io", "storage", "threads", "handles"]) {
    assert.doesNotMatch(JSON.stringify(element("resource-chart-" + metric).children), /NaN|Infinity/);
}

// Switching while a polling cycle is pending must discard all of its obsolete results.
run("resourceRefresh()");
assert.equal(requests.length, 6);
element("resource-resolution").listeners.change({target: {value: "60m"}});
assert.equal(requests.length, 6);
finishCycle(3, response);
assert.equal(element("resource-message").textContent, "Loading selected resolution…");
assert.equal([...timers.values()][0].delay, 0);
run("resourceRefresh()");
for (const request of requests.slice(6)) {
    assert.equal(JSON.parse(request.data).payload.resolution, "60m");
}
finishCycle(6, {...response, resolution: "60m"});
assert.equal([...timers.values()][0].delay, 60000);

// Hidden tabs pause polling; a complete outage backs off without overlapping requests.
context.document.hidden = true;
run("resourceRefresh()");
assert.equal(requests.length, 9);
context.document.hidden = false;
documentEvents.visibilitychange();
assert.equal(requests.length, 12);
finishCycle(9, {components: {}});
assert.match(element("resource-message").textContent, /Measurements are unavailable/);
assert.equal([...timers.values()][0].delay, 10000);
windowEvents.pagehide();
run("resourceRefresh()");
assert.equal(requests.length, 12);
assert.equal(timers.size, 0);
windowEvents.pageshow({persisted: true});
assert.equal(requests.length, 15, "Back navigation left polling stopped");
finishCycle(12, response);
windowEvents.pagehide();
scopes = [];
run("auditReady()");
assert.equal(requests.length, 15);

// Expired credentials stop the entire cycle and report the authentication failure once.
scopes = ["stats"];
let authErrors = 0;
context.commonFunctionError = () => { authErrors++; };
windowEvents.pageshow({persisted: true});
for (const request of requests.slice(15)) {
    request.error({status: 403});
    request.complete({}, "error");
}
assert.equal(authErrors, 1);
assert.equal(timers.size, 0);
assert.equal(run("resourceDashboard.pending"), false);
run("resourceRefresh()");
assert.equal(requests.length, 18);
console.log("PASS: parallel scoped resource RPCs, partial failures, resolution races and nonoverlapping polling");

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
assert.equal(requests.length, 1, "Overlapping requests were sent");
assert.equal(requests[0].url, "/api/v1/resources/3s");
assert.throws(() => run('auditRequest({method: "GET", resource: "resources/5m"})'));
assert.throws(() => run('auditRequest({method: "PUT", resource: "resources/3s"})'));

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
requests[0].success(response);
requests[0].complete({}, "success");
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

// Switching while a request is pending must discard that request's obsolete result.
run("resourceRefresh()");
assert.equal(requests.length, 2);
element("resource-resolution").listeners.change({target: {value: "60m"}});
assert.equal(requests.length, 2);
requests[1].success(response);
assert.equal(element("resource-message").textContent, "Loading selected resolution…");
requests[1].complete({}, "success");
assert.equal([...timers.values()][0].delay, 0);
run("resourceRefresh()");
assert.equal(requests.at(-1).url, "/api/v1/resources/60m");
requests.at(-1).success({...response, resolution: "60m"});
requests.at(-1).complete({}, "success");
assert.equal([...timers.values()][0].delay, 60000);

// Hidden tabs pause requests; repeated failures remain visible and back off.
context.document.hidden = true;
run("resourceRefresh()");
assert.equal(requests.length, 3);
context.document.hidden = false;
documentEvents.visibilitychange();
assert.equal(requests.length, 4);
requests.at(-1).error({status: 503});
requests.at(-1).complete({}, "error");
assert.match(element("resource-message").textContent, /may be stale/);
assert.equal([...timers.values()][0].delay, 10000);
windowEvents.pagehide();
run("resourceRefresh()");
assert.equal(requests.length, 4);
assert.equal(timers.size, 0);
windowEvents.pageshow({persisted: true});
assert.equal(requests.length, 5, "Back navigation left polling stopped");
requests.at(-1).success(response);
requests.at(-1).complete({}, "success");
windowEvents.pagehide();
scopes = [];
run("auditReady()");
assert.equal(requests.length, 5);
console.log("PASS: combined resource charts, missing values, resolution races and nonoverlapping scoped polling");

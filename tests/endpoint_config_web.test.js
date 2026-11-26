'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

/** @brief Exercise endpoint drafts, atomic saves, failures, removal and read-only access. */
async function testEndpointEditor(kind) {
    /** @brief Minimal DOM element with safe rendering and native dialog state. */
    class Element {
        constructor() {
            this.children = [];
            this.listeners = {};
            this.value = '';
            this.textContent = '';
            this.open = false;
            this.valid = true;
        }
        /** @brief Reject HTML injection in the endpoint renderer. */
        set innerHTML(value) { throw new Error('Endpoint values must use textContent'); }
        /** @brief Register an event handler for simulated user actions. */
        addEventListener(event, handler) { this.listeners[event] = handler; }
        /** @brief Assign an accessible label. */
        setAttribute(name, value) { this[name] = value; }
        /** @brief Append a row, cell or control. */
        appendChild(child) { this.children.push(child); return child; }
        /** @brief Create a table row. */
        insertRow() { return this.appendChild(new Element()); }
        /** @brief Create a table cell. */
        insertCell() { return this.appendChild(new Element()); }
        /** @brief Clear previously rendered rows. */
        replaceChildren() { this.children = []; }
        /** @brief Simulate native modal opening. */
        showModal() { this.open = true; }
        /** @brief Simulate native modal closing. */
        close() { this.open = false; }
        /** @brief Simulate required-field validation. */
        reportValidity() { return this.valid; }
    }
    const elements = new Map();
    /** @brief Return a stable element for a page ID. */
    function element(id) {
        if (!elements.has(id)) {
            elements.set(id, new Element());
        }
        return elements.get(id);
    }
    let controlScope = true;
    let pending;
    let confirmed = true;
    let reloadScheduled = false;
    let activityReads = 0;
    const context = vm.createContext({
        document: {
            getElementById: element,
            createElement: () => new Element(),
            querySelectorAll: () => [
                ...elements.values(),
                ...element('endpoints-list').children.flatMap(row => row.children.flatMap(cell => cell.children))
            ]
        },
        setTimeout: () => { reloadScheduled = true; },
        crypto: {randomUUID: () => "new-id"},
        auditHasScope: () => controlScope,
        loadEndpointActivity: () => { activityReads++; },
        auditRequest: (request, handlers) => { pending = {request, handlers}; },
        auditEnhanceContent: () => {},
        auditConfirm: async () => confirmed,
        commonFunctionError: () => {},
        showToast: () => {}
    });
    element('endpoint-config').dataset = {kind};
    const script = fs.readFileSync(path.join(__dirname, '../ul3_web/var/www/ulogeventaudit3/global/assets/js/endpoints.js'), 'utf8');
    vm.runInContext(script, context);
    /** @brief Run a page action in the script's own scope. */
    function run(source) { return vm.runInContext(source, context); }
    /** @brief Complete a captured request with the backend result. */
    function respond(result) {
        const handlers = pending.handlers;
        if (result.ok && pending.request.method === 'PUT') {
            result[kind] = pending.request.payload[kind];
            result.restartRequired = true;
        }
        handlers.success(result);
        handlers.complete({}, 'success');
    }
    /** @brief Submit the editor as a validated browser form. */
    function submit() { element('endpoint-form').listeners.submit({preventDefault() {}}); }
    const original = {id: 'first', Description: '<img src=x onerror=alert(1)>',
        ...(kind === 'inputs' ? {ListenAddr: '127.0.0.1', ListenPort: 10514} : {
            Host: '127.0.0.1', Port: 9999, TransmissionMode: 0, TimeoutSecs: 10,
            ReconnectSleepTimeInSecs: 3, MaxQueuedItems: 500000, QueuePushTimeoutInMS: 0})};
    run('auditReady()');
    assert.equal(pending.request.method, 'GET');
    assert.equal(activityReads, 1);
    assert.equal(element('endpoint-add').disabled, true);
    respond({ok: true, [kind]: [original]});
    const row = element('endpoints-list').children[0];
    assert.equal(row.children[0].textContent, original.Description);
    assert.equal(row.children[3].children.length, 2);
    assert.equal(element('endpoint-add').disabled, false);

    // Cancel never mutates the saved list, including edited names and templates.
    row.children[3].children[0].listeners.click();
    assert.equal(element('endpoint-dialog').open, true);
    run('auditReady()');
    assert.equal(activityReads, 1);
    assert.equal(element('endpoint-Description').value, original.Description);
    element('endpoint-Description').value = 'discarded';
    element('endpoint-cancel').listeners.click();
    assert.equal(row.children[0].textContent, original.Description);
    run('openEndpointEditor(0)');
    assert.equal(element('endpoint-Description').value, original.Description);
    element('endpoint-Description').value = 'updated';
    submit();
    assert.equal(pending.request.method, 'PUT');
    assert.equal(pending.request.resource, `log2json/${kind}/config`);
    assert.equal(pending.request.payload[kind][0].Description, 'updated');
    assert.equal(pending.request.payload[kind][0].id, 'first');
    assert.equal(typeof pending.request.payload[kind][0][kind === 'inputs' ? 'ListenPort' : 'Port'], 'number');
    assert.equal(element('endpoint-cancel').disabled, true);
    let cancelled = false;
    element('endpoint-dialog').listeners.cancel({preventDefault() { cancelled = true; }});
    assert.equal(cancelled, true);
    respond({ok: false, message: 'Invalid regex'});
    assert.equal(element('endpoint-dialog').open, true);
    assert.equal(element('endpoint-error').textContent, 'Invalid regex');
    assert.equal(row.children[0].textContent, original.Description);
    assert.equal(element('endpoint-cancel').disabled, false);
    submit();
    respond({ok: true});
    assert.equal(element('endpoint-dialog').open, false);
    assert.equal(element('endpoints-list').children[0].children[0].textContent, 'updated');

    // A new draft retains its content on transport/CSRF failure and can be retried.
    element('endpoint-add').listeners.click();
    assert.equal(element('endpoint-Description').value, kind === 'inputs' ? 'Syslog TCP input' : 'JSON TCP output');
    element('endpoint-Description').value = 'new endpoint';
    element('endpoint-form').valid = false;
    const previous = pending;
    submit();
    assert.equal(pending, previous);
    element('endpoint-form').valid = true;
    submit();
    assert.equal(pending.request.payload[kind].length, 2);
    assert.equal(pending.request.payload[kind][1].id, 'new-id');
    pending.handlers.error({status: 503});
    pending.handlers.complete({}, 'error');
    assert.equal(element('endpoint-dialog').open, true);
    assert.equal(element('endpoint-Description').value, 'new endpoint');
    assert.equal(element('endpoint-cancel').disabled, false);
    assert.equal(element('endpoints-list').children.length, 1);
    submit();
    respond({ok: true});
    assert.equal(element('endpoints-list').children.length, 2);

    confirmed = false;
    const saved = pending;
    await run('removeEndpoint(0)');
    assert.equal(pending, saved);
    confirmed = true;
    await run('removeEndpoint(0)');
    assert.equal(pending.request.payload[kind].length, 1);
    assert.equal(pending.request.payload[kind][0].Description, 'new endpoint');
    respond({ok: true});
    await run('removeEndpoint(0)');
    assert.equal(pending.request.payload[kind].length, 0);
    respond({ok: true});
    assert.equal(element('endpoints-list').children.length, 0);

    // Restart requires confirmation and preserves normal request error handling.
    assert.equal(element('endpoint-restart').hidden, false);
    confirmed = false;
    const beforeRestart = pending;
    await run('restartLog2JSON()');
    assert.equal(pending, beforeRestart);
    confirmed = true;
    await run('restartLog2JSON()');
    assert.equal(pending.request.resource, 'log2json/reload');
    assert.equal(pending.request.method, 'POST');
    respond(false);
    assert.equal(reloadScheduled, false);

    // Readers can inspect definitions but cannot create, edit or remove endpoints.
    controlScope = false;
    run('auditReady()');
    respond({ok: true, [kind]: [original]});
    assert.equal(activityReads, 2);
    assert.equal(element('endpoint-add').hidden, true);
    assert.equal(element('endpoints-list').children[0].children[3].children.length, 0);
    const read = pending;
    run('openEndpointEditor(); saveEndpoints([])');
    await run('removeEndpoint(0)');
    await run('restartLog2JSON()');
    assert.equal(element('endpoint-reload').hidden, true);
    assert.equal(pending, read);
    assert.equal(element('endpoint-dialog').open, false);
    controlScope = true;
    await run('restartLog2JSON()');
    respond(true);
    assert.equal(reloadScheduled, true);
    assert.equal(element('endpoint-reload').disabled, true);
    const restarting = pending;
    run('openEndpointEditor(); saveEndpoints([]); auditReady()');
    assert.equal(pending, restarting);
    console.log(`PASS: ${kind} editor drafts, save/retry, removal and read-only scope`);
}
(async () => { await testEndpointEditor('inputs'); await testEndpointEditor('outputs'); })().catch(error => { console.error(error); process.exitCode = 1; });

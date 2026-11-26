'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

/** @brief Exercise module drafts, atomic saves, failures, removal and read-only access. */
module.exports = async function testModuleEditor() {
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
        /** @brief Reject HTML injection in the module renderer. */
        set innerHTML(value) { throw new Error('Module values must use textContent'); }
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
    const context = vm.createContext({
        document: {
            getElementById: element,
            createElement: () => new Element(),
            querySelectorAll: () => [
                ...elements.values(),
                ...element('modules-list').children.flatMap(row => row.children.flatMap(cell => cell.children))
            ]
        },
        auditHasScope: () => controlScope,
        auditRequest: (request, handlers) => { pending = {request, handlers}; },
        auditEnhanceContent: () => {},
        auditConfirm: async () => confirmed,
        commonFunctionError: () => {},
        showToast: () => {}
    });
    const page = fs.readFileSync(path.join(__dirname, '../ul3_web/var/www/ulogeventaudit3/webroot/mods/log2json/modules.html'), 'utf8');
    assert.match(page, /<dialog[^>]+id="module-dialog"/);
    assert.match(page, /<tbody id="modules-list">/);
    vm.runInContext([...page.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(match => match[1]).join('\n'), context);
    /** @brief Run a page action in the script's own scope. */
    function run(source) { return vm.runInContext(source, context); }
    /** @brief Complete a captured request with the backend result. */
    function respond(result) {
        const handlers = pending.handlers;
        handlers.success(result);
        handlers.complete({}, 'success');
    }
    /** @brief Submit the editor as a validated browser form. */
    function submit() { element('module-form').listeners.submit({preventDefault() {}}); }
    const original = {name: '<img src=x onerror=alert(1)>', loggerRegex: '^sshd$', messageRegex: '(.*)', jsonTemplate: '{"message":(string)<$1>}'};
    run('auditReady()');
    assert.equal(pending.request.scope, 'stats');
    assert.equal(element('module-add').disabled, true);
    respond({ok: true, modules: [original]});
    const row = element('modules-list').children[0];
    assert.equal(row.children[0].textContent, original.name);
    assert.equal(row.children[3].children.length, 2);
    assert.equal(element('module-add').disabled, false);

    // Cancel never mutates the saved list, including edited names and templates.
    row.children[3].children[0].listeners.click();
    assert.equal(element('module-dialog').open, true);
    assert.equal(element('module-name').value, original.name);
    element('module-name').value = 'discarded';
    element('module-cancel').listeners.click();
    assert.equal(row.children[0].textContent, original.name);
    run('openModuleEditor(0)');
    assert.equal(element('module-name').value, original.name);
    element('module-name').value = 'updated';
    submit();
    assert.equal(pending.request.scope, 'control');
    assert.equal(pending.request.method, 'SyslogModules');
    assert.equal(pending.request.payload.modules[0].name, 'updated');
    assert.equal(element('module-cancel').disabled, true);
    let cancelled = false;
    element('module-dialog').listeners.cancel({preventDefault() { cancelled = true; }});
    assert.equal(cancelled, true);
    respond({ok: false, message: 'Invalid regex'});
    assert.equal(element('module-dialog').open, true);
    assert.equal(element('module-error').textContent, 'Invalid regex');
    assert.equal(row.children[0].textContent, original.name);
    assert.equal(element('module-cancel').disabled, false);
    submit();
    respond({ok: true});
    assert.equal(element('module-dialog').open, false);
    assert.equal(element('modules-list').children[0].children[0].textContent, 'updated');

    // A new draft retains its content on transport/CSRF failure and can be retried.
    element('module-add').listeners.click();
    assert.equal(element('module-name').value, '');
    element('module-name').value = 'new module';
    element('module-form').valid = false;
    const previous = pending;
    submit();
    assert.equal(pending, previous);
    element('module-form').valid = true;
    submit();
    assert.equal(pending.request.payload.modules.length, 2);
    pending.handlers.error({status: 503});
    pending.handlers.complete({}, 'error');
    assert.equal(element('module-dialog').open, true);
    assert.equal(element('module-name').value, 'new module');
    assert.equal(element('module-cancel').disabled, false);
    assert.equal(element('modules-list').children.length, 1);
    submit();
    respond({ok: true});
    assert.equal(element('modules-list').children.length, 2);

    confirmed = false;
    const saved = pending;
    await run('removeSyslogModule(0)');
    assert.equal(pending, saved);
    confirmed = true;
    await run('removeSyslogModule(0)');
    assert.equal(pending.request.payload.modules.length, 1);
    assert.equal(pending.request.payload.modules[0].name, 'new module');
    respond({ok: true});
    await run('removeSyslogModule(0)');
    assert.equal(pending.request.payload.modules.length, 0);
    respond({ok: true});
    assert.equal(element('modules-list').children.length, 0);

    // Readers can inspect definitions but cannot create, edit or remove modules.
    controlScope = false;
    run('auditReady()');
    respond({ok: true, modules: [original]});
    assert.equal(element('module-add').hidden, true);
    assert.equal(element('modules-list').children[0].children[3].children.length, 0);
    const read = pending;
    run('openModuleEditor(); saveSyslogModules([])');
    await run('removeSyslogModule(0)');
    assert.equal(pending, read);
    assert.equal(element('module-dialog').open, false);
    console.log('PASS: module list, modal drafts, atomic save/retry, removal and read-only scope');
};

'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
let captured;
let scopes = ['stats', 'control'];
let sessionReads = 0;
const context = vm.createContext({
    document: {querySelectorAll: () => scopes.map(textContent => ({textContent}))},
    $: {ajax: options => {
        if (options.url === '/api/v1/session') {
            sessionReads++;
            return Promise.resolve({csrfToken: 'csrf-' + sessionReads});
        }
        captured = options;
        return options;
    }}
});
vm.runInContext(fs.readFileSync(path.join(root, 'ul3_web/var/www/ulogeventaudit3/global/assets/js/audit.js'), 'utf8'), context);
async function request(value, handlers = {}) {
    context.input = value;
    context.handlers = handlers;
    await vm.runInContext('auditRequest(input, handlers)', context);
    return captured;
}
(async () => {
    let options = await request({method: 'GET', resource: 'alerts/rules'});
    assert.equal(options.type, 'GET');
    assert.equal(options.url, '/api/v1/alerts/rules');
    assert.equal(options.data, undefined);
    options = await request({method: 'GET', resource: 'alerts/actions/item', payload: {name: 'acción & test'}});
    assert.deepEqual(JSON.parse(decodeURIComponent(options.url.split('?')[1])), {name: 'acción & test'});
    assert.equal(sessionReads, 0);
    options = await request({method: 'DELETE', resource: 'alerts/rules', payload: {pos: 0}});
    assert.deepEqual(JSON.parse(options.data), {pos: 0});
    assert.equal(options.headers['X-CSRF-Token'], 'csrf-1');
    assert.equal(options.contentType, 'application/json');
    options = await request({method: 'POST', resource: 'alerts/rules/save'});
    assert.equal(options.headers['X-CSRF-Token'], 'csrf-2');
    await assert.rejects(request({method: 'GET', resource: '../session'}));
    await assert.rejects(request({method: 'PATCH', resource: 'alerts/rules'}));
    scopes = ['stats'];
    let denied = false;
    captured = null;
    await request({method: 'POST', resource: 'alerts/rules/save'}, {error: xhr => { denied = xhr.status === 403; }});
    assert.equal(denied, true);
    assert.equal(captured, null);
    assert.equal(sessionReads, 2);
    console.log('PASS: JSON requests, Unicode query arguments, fresh CSRF after renewal, and scope gating');
})().catch(error => {console.error(error); process.exitCode = 1;});

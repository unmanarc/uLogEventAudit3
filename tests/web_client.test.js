'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
let captured;
const context = vm.createContext({window: {csrfToken: 'test-csrf'},
    $: {ajax: options => { captured = options; return options; }}});
vm.runInContext(fs.readFileSync(path.join(root, 'uanlz_web/var/www/uauditweb3/assets/js/audit.js'), 'utf8'), context);
function request(value) {
    context.input = value;
    vm.runInContext('auditRequest(input, {})', context);
    return captured;
}
let options = request({target: 'ALERTS', remoteMethod: 'stats.ReadRunningRules'});
assert.equal(options.type, 'GET');
assert.equal(options.url, '/api/v1/alerts/rules');
assert.equal(options.data, undefined);
options = request({target: 'ALERTS', remoteMethod: 'stats.ReadRunningAction', payload: {name: 'acción & test'}});
assert.equal(options.type, 'GET');
assert.deepEqual(JSON.parse(decodeURIComponent(options.url.split('?')[1])), {name: 'acción & test'});
assert.equal(options.data, undefined);
options = request({target: 'ALERTS', remoteMethod: 'control.RemoveRule', payload: {pos: 0}});
assert.equal(options.type, 'DELETE');
assert.equal(options.url, '/api/v1/alerts/rules');
assert.deepEqual(JSON.parse(options.data), {pos: 0});
assert.equal(options.headers['X-CSRF-Token'], 'test-csrf');
assert.equal(options.contentType, 'application/json');
assert.throws(() => request({target: 'ALERTS', remoteMethod: 'control.Unknown'}));
// Client routes must agree with the server's allowlist.
const serverRoutes = fs.readFileSync(path.join(root, 'uanlz_web/src/rest_routes.inc'), 'utf8');
for (const [, verb, resource, target, method] of serverRoutes.matchAll(/Method::(\w+), "([^"]+)", "([^"]+)", "([^"]+)"/g)) {
    options = request({target, remoteMethod: method});
    assert.equal(options.type, verb);
    assert.equal(options.url, '/api/v1/'+resource);
}
console.log('PASS: browser GET encoding, JSON mutations, CSRF and all server route mappings');

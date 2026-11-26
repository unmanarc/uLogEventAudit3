'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
let captured;
let scopes = ['stats', 'control'];
let sessionReads = 0;
let sessionOptions;
const context = vm.createContext({
    document: {querySelectorAll: () => scopes.map(textContent => ({textContent}))},
    $: {ajax: options => {
        if (options.url === '/api/v1/session') {
            sessionReads++;
            sessionOptions = options;
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
    // Shared header assets must resolve from nested pages as well as the homepage.
    const header = fs.readFileSync(path.join(root, 'ul3_web/var/www/ulogeventaudit3/webroot/includes/layout/inc.header.html'), 'utf8');
    const authSource = header.match(/<script src="([^"]*auth\.js)"/)[1];
    for (const page of ['/', '/mods/log2json/events.html', '/mods/alerts/rules.html']) {
        assert.equal(new URL(authSource, 'https://audit.example' + page).pathname, '/auth/assets/js/auth.js');
    }
    const welcome = {textContent: 'session-user'};
    context.document.getElementById = id => id === 'welcome' ? welcome : null;
    context.fetch = async url => {
        assert.equal(url, '/api/v1/session');
        return {ok: true};
    };
    let readyCalls = 0;
    context.auditReady = () => readyCalls++;
    // Evaluate header scripts too, so an accidental initializer override is caught.
    for (const script of header.matchAll(/<script>([\s\S]*?)<\/script>/g)) {
        vm.runInContext(script[1], context);
    }
    await vm.runInContext('ajaxLoadInfo()', context);
    assert.equal(welcome.textContent, 'session-user');
    context.ufad3_accountPublicData = {body: {displayName: 'María <Admin>'}};
    await vm.runInContext('ajaxLoadInfo()', context);
    assert.equal(welcome.textContent, 'María <Admin>');
    assert.equal(readyCalls, 2);

    let options = await request({scope: 'stats', target: 'ALERTS', method: 'ReadRunningRules'});
    assert.equal(options.type, 'POST');
    assert.equal(options.url, '/api/v1/remote.stats');
    assert.deepEqual(JSON.parse(options.data), {target: 'ALERTS', method: 'ReadRunningRules', payload: {}});
    options = await request({scope: 'stats', target: 'ALERTS', method: 'ReadRunningAction', payload: {name: 'acción & test'}});
    assert.deepEqual(JSON.parse(options.data), {target: 'ALERTS', method: 'ReadRunningAction', payload: {name: 'acción & test'}});
    options = await request({scope: 'stats', target: 'WEB', method: 'ResourceMonitor', payload: {resolution: '1m'}});
    assert.equal(options.url, '/api/v1/remote.stats');
    assert.deepEqual(JSON.parse(options.data), {target: 'WEB', method: 'ResourceMonitor', payload: {resolution: '1m'}});
    assert.equal(sessionReads, 0);
    options = await request({scope: 'control', target: 'ALERTS', method: 'RemoveRule', payload: {pos: 0}});
    assert.equal(options.url, '/api/v1/remote.control');
    assert.deepEqual(JSON.parse(options.data), {target: 'ALERTS', method: 'RemoveRule', payload: {pos: 0}});
    assert.equal(options.headers['X-CSRF-Token'], 'csrf-1');
    assert.equal(options.contentType, 'application/json');
    // jQuery cache:false appends a non-JSON `?_=...` query rejected by Mantids30.
    // The session endpoint already sends Cache-Control: no-store.
    assert.equal(sessionOptions.cache, true);
    assert.equal(sessionOptions.data, undefined);
    options = await request({scope: 'control', target: 'ALERTS', method: 'SaveRunningRules'});
    assert.equal(options.headers['X-CSRF-Token'], 'csrf-2');
    await assert.rejects(request({method: 'GET', resource: '../session'}));
    await assert.rejects(request({scope: 'stats', target: 'ALERTS', method: 'control.ReloadApp'}));
    await assert.rejects(request({scope: 'stats', target: 'OTHER', method: 'SystemStats'}));
    scopes = ['stats'];
    let denied = false;
    captured = null;
    await request({scope: 'control', target: 'ALERTS', method: 'SaveRunningRules'}, {error: xhr => { denied = xhr.status === 403; }});
    assert.equal(denied, true);
    assert.equal(captured, null);
    assert.equal(sessionReads, 2);
    // UI request tracking must finish on backend errors and failed CSRF renewal.
    scopes = ['stats', 'control'];
    const starts = [], finishes = [];
    context.auditRequestStarted = scope => starts.push(scope);
    context.auditRequestFinished = failed => finishes.push(failed);
    options = await request({scope: 'stats', target: 'ALERTS', method: 'ReadRunningRules'});
    options.complete({}, 'error');
    assert.deepEqual(starts, ['stats']);
    assert.deepEqual(finishes, [true]);
    const originalAjax = context.$.ajax;
    context.$.ajax = options => options.url === '/api/v1/session'
        ? Promise.reject({status: 503}) : originalAjax(options);
    let renewalError = false;
    let renewalComplete = 0;
    await request({scope: 'control', target: 'ALERTS', method: 'SaveRunningRules'}, {
        error: () => { renewalError = true; },
        complete: (xhr, status) => {
            assert.equal(xhr.status, 503);
            assert.equal(status, 'error');
            renewalComplete++;
        }
    });
    assert.equal(renewalError, true);
    assert.equal(renewalComplete, 1);
    assert.deepEqual(starts, ['stats', 'control']);
    assert.deepEqual(finishes, [true, true]);

    // A queue that drains from >50% to empty must clear both halves and its label.
    const elements = new Map();
    function element(id) {
        if (!elements.has(id)) elements.set(id, {
            style: {setProperty(name, value) { this[name] = value; }}, dataset: {}, attributes: {}, textContent: '',
            classList: {remove() {}, add() {}},
            setAttribute(name, value) { this.attributes[name] = String(value); },
            removeAttribute(name) { delete this.attributes[name]; },
            querySelector(selector) { return element(selector); }
        });
        return elements.get(id);
    }
    const progressContext = vm.createContext({document: {
        getElementById: element,
        querySelectorAll: () => [element('queue')]
    }});
    vm.runInContext(fs.readFileSync(path.join(root, 'ul3_web/var/www/ulogeventaudit3/global/assets/js/progress.js'), 'utf8'), progressContext);
    vm.runInContext('fillPercentage("value", "queue", "left", "right", 90); redrawProgressBars()', progressContext);
    assert.equal(element('queue').style['--queue-value'], '90%');
    vm.runInContext('fillPercentage("value", "queue", "left", "right", 0); redrawProgressBars()', progressContext);
    assert.equal(element('queue').style['--queue-value'], '0%');
    assert.equal(element('queue').attributes['aria-valuenow'], '0');
    assert.equal(element('.progress-left .progress-bar').style.transform, 'rotate(0deg)');
    assert.equal(element('.progress-right .progress-bar').style.transform, 'rotate(0deg)');
    vm.runInContext('fillPercentage("value", "queue", "left", "right", NaN)', progressContext);
    assert.equal(element('value').textContent, '—');
    assert.equal(element('queue').attributes['aria-valuenow'], undefined);

    // The host list must render the backend's `host` member into the real tbody.
    const hostElements = new Map(), appended = [];
    const hostContext = vm.createContext({
        document: {getElementById: id => ({innerHTML: ''})},
        $: selector => ({append: html => appended.push({selector, html}), text: value => hostElements.set(selector, value)})
    });
    const hostPage = fs.readFileSync(path.join(root, 'ul3_web/var/www/ulogeventaudit3/webroot/mods/log2json/eventsph.html'), 'utf8');
    const scripts = [...hostPage.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(match => match[1]).join('\n');
    vm.runInContext(scripts, hostContext);
    vm.runInContext('drawEventsPerHostStats([{host: "audit-01", ip: "127.0.0.1", events: {pending: 3, processed: 42, dropped: 0}}])', hostContext);
    assert.equal(appended[0].selector, '#tbody-log2json-eventsperhost');
    assert.equal(hostElements.get('#log2json-eventsperhost-0-0'), 'audit-01/127.0.0.1');
    assert.equal(hostElements.get('#log2json-eventsperhost-0-2'), 42);
    // Combined input/output pages load saved configuration and running activity together.
    for (const [subsystem, kind] of [['alerts', 'inputs'], ['log2json', 'inputs'], ['log2json', 'outputs']]) {
        const requests = [], values = new Map(), pageElements = new Map();
        /** @brief Preserve element state across configuration and activity renderers. */
        function pageElement(id) {
            if (!pageElements.has(id)) {
                pageElements.set(id, {innerHTML: '', style: {}});
            }
            return pageElements.get(id);
        }
        const pageContext = vm.createContext({
            document: {getElementById: pageElement},
            $: selector => ({append() {}, text: value => values.set(selector, value)}),
            auditRequest: (request, handlers) => { requests.push({request, handlers}); },
            commonFunctionError() {},
            fillPercentage() {},
            redrawProgressBars() {}
        });
        const page = fs.readFileSync(path.join(root, `ul3_web/var/www/ulogeventaudit3/webroot/mods/${subsystem}/${kind}.html`), 'utf8');
        const pageScripts = [...page.matchAll(/<script>([\s\S]*?)<\/script>/g)]
            .map(match => match[1].replace(/\{\{[\s\S]*?\}\}/g, '')).join('\n');
        vm.runInContext(pageScripts, pageContext);
        if (subsystem === 'alerts') {
            vm.runInContext('auditReady()', pageContext);
            assert.deepEqual(requests.map(item => item.request.method), ['ReadInputsFileConfig', 'ReadRunningInputs']);
            requests[0].handlers.success({modified: true, inputs: [{id: 'test', Description: 'Saved listener', ListenAddr: '127.0.0.1', ListenPort: 9999}]});
            requests[1].handlers.success([{description: 'Running listener', clients: [{ip: '192.0.2.1', processedLinesCount: 7, droppedElements: 2}]}]);
            assert.equal(values.get('#span-alerts-inputsconfig-description-0'), 'Saved listener');
            assert.equal(values.get('#span-alerts-input-description-0'), 'Running listener');
            assert.equal(values.get('#alerts-inputs-0-0-1'), 7);
            assert.equal(pageElement('span-alerts-inputsmodified-body').style.display, 'block');
            requests[0].handlers.success({modified: false, inputs: []});
            assert.equal(pageElement('span-alerts-inputsmodified-body').style.display, 'none');
        } else {
            assert.match(page, /id="endpoints-list"/);
            assert.match(page, /src="\/assets\/js\/endpoints.js"/);
            vm.runInContext('loadEndpointActivity()', pageContext);
            assert.equal(requests[0].request.method, kind === 'inputs' ? 'Inputs' : 'Outputs');
            if (kind === 'inputs') {
                requests[0].handlers.success({inputs: [{description: 'Running input', processedLinesCount: 42, invalidLinesCount: 3, clients: []}]});
                assert.equal(values.get('#span-log2json-input-proclines-0'), 42);
            } else {
                requests[0].handlers.success({outputs: [{id: '127.0.0.1:9999', connected: true, queue: {size: 1, maxItems: 10, dropped: 2, processed: 42}}]});
                assert.equal(values.get('#span-log2json-output-processed-0'), 42);
                assert.equal(values.get('#span-log2json-output-perctext-0'), '1/10');
            }
        }
    }
    assert.doesNotMatch(header, /\/mods\/(alerts|log2json)\/(inputsconfig|outputsconfig)\.html/);
    await require('./syslog_modules_web.test.js')();
    console.log('PASS: JSON requests, Unicode RPC payloads, fresh CSRF after renewal, and scope gating');
    console.log('PASS: nested-page auth path, profile display name, and page initialization');
    console.log('PASS: UI request lifecycle, queue reset/unavailable states, and per-host rendering');
})().catch(error => {console.error(error); process.exitCode = 1;});

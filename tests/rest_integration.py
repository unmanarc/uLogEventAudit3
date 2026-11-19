#!/usr/bin/env python3
"""Exercise the three real daemons using a temporary APISync fixture and TLS certificates.
Checks APISync, authentication, the REST console and audit-to-action processing.
No existing configuration, account, or running service is modified.
"""
import argparse
import base64
import hashlib
import hmac
import http.client
import http.server
import json
import os
import re
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

ROOT = Path(__file__).resolve().parents[1]
SECRET = 'integration-only-secret-with-at-least-32-bytes'


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def token(scopes, app='ULOGEVENTAUDIT3', expired=False, subject='test-user'):
    def b64(value):
        return base64.urlsafe_b64encode(value).rstrip(b'=')
    head = b64(json.dumps({'alg': 'HS256', 'typ': 'JWT'}).encode())
    body = b64(json.dumps({'sub': subject, 'app': app, 'type': 'access',
                          'scopes': scopes, 'jti': 'test-csrf-id', 'iat': int(time.time())-60,
                          'exp': int(time.time()) + (-1 if expired else 3600)}).encode())
    data = head+b'.'+body
    return (data+b'.'+b64(hmac.new(SECRET.encode(), data, hashlib.sha256).digest())).decode()


class AppSync(http.server.BaseHTTPRequestHandler):
    requests = []

    def log_message(self, *args):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        assert body['APIKEY'] == 'integration-app-key'
        assert 'APP=ULOGEVENTAUDIT3' in self.path
        name = urllib.parse.urlsplit(self.path).path.rsplit('/', 1)[-1]
        self.requests.append((name, body))
        if name == 'getApplicationJWTConfig':
            result = {'tokensConfiguration': {'signAlgorithm': 'HS256'}}
        elif name == 'getApplicationJWTValidationKey':
            result = SECRET
        elif name == 'updateAccessControlContext':
            assert {s['id'] for s in body['scopes']} == {'stats', 'control'}
            result = {}
        else:
            raise AssertionError(name)
        payload = json.dumps(result).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, default=ROOT/'build/mantids30')
    parser.add_argument('--library-dir', type=Path, help='Directory containing libMantids30 shared libraries')
    args = parser.parse_args()
    if args.library_dir:
        os.environ['LD_LIBRARY_PATH'] = str(args.library_dir.resolve()) + ':' + os.environ.get('LD_LIBRARY_PATH', '')
    processes = []
    logfiles = []
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), AppSync)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix='ul3-rest-') as temp:
        tmp = Path(temp)
        try:
            subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                            '-keyout', str(tmp/'server.key'), '-out', str(tmp/'server.crt'),
                            '-days', '1', '-subj', '/CN=localhost',
                            '-addext', 'subjectAltName=DNS:localhost,IP:127.0.0.1'],
                           check=True, capture_output=True)
            web_port, rpc_port, alert_port, input_port = [free_port() for _ in range(4)]
            config = f'''
Logs {{ ShowColors false ShowDate false }}
Authentication {{ AppName ULOGEVENTAUDIT3 APIKey integration-app-key }}
RPCServer {{
    AlertsApiKey integration-alert-key
    Log2JSONApiKey integration-log-key
    ListenAddr 127.0.0.1
    ListenPort {rpc_port}
    CertFile {tmp}/server.crt
    KeyFile {tmp}/server.key
}}
WebService {{
    Listener_TLS {{
        Protocol TLS
        ListenAddr 127.0.0.1
        ListenPort {web_port}
        TLS {{ CertFile {tmp}/server.crt KeyFile {tmp}/server.key }}
    }}
    Logs {{ Dir {tmp} File access.log }}
    ResourcesPath {ROOT}/ul3_web/var/www/ulogeventaudit3/webroot
    OverlappedDirectories {{ /assets {{ Path {ROOT}/ul3_web/var/www/ulogeventaudit3/global/assets }} }}
    JWT {{ UseAPISync true }}
    APISync {{ APISyncHost 127.0.0.1 APISyncPort {server.server_port} UseTLS false }}
    API {{ Origins https://localhost:{web_port} }}
    Threads {{ UseThreadPool false MaxConcurrentClients 32 }}
    Redirections {{ /login https://login.example.test/ }}
}}
'''
            (tmp/'webserver.conf').write_text(config)
            (tmp/'webserver.conf').chmod(0o600)

            def launch(name, directory):
                log = open(tmp/(name+'.log'), 'w+')
                logfiles.append(log)
                proc = subprocess.Popen([str(args.build_dir.resolve()/name/name), '-c', str(directory)],
                                        stdout=log, stderr=subprocess.STDOUT)
                processes.append(proc)
                return proc

            launch('ul3_web', tmp)
            context = ssl.create_default_context(cafile=str(tmp/'server.crt'))

            def request(path, jwt=None, method='GET', data=None, csrf=None):
                conn = http.client.HTTPSConnection('localhost', web_port, context=context, timeout=5)
                headers = {'User-Agent': 'curl/8.0', 'Origin': f'https://localhost:{web_port}'}
                if jwt:
                    headers['Cookie'] = 'AccessToken='+jwt
                if csrf:
                    headers['X-CSRF-Token'] = csrf
                if method == 'GET' and data is not None:
                    path += '?'+urllib.parse.quote(json.dumps(data), safe='')
                body = None if data is None or method == 'GET' else json.dumps(data)
                if body:
                    headers['Content-Type'] = 'application/json'
                conn.request(method, path, body, headers)
                response = conn.getresponse()
                result = response.status, response.read().decode()
                conn.close()
                return result

            def eventually(check, description):
                deadline = time.monotonic()+15
                while time.monotonic() < deadline:
                    try:
                        if check():
                            return
                    except (ConnectionError, OSError, http.client.HTTPException):
                        pass
                    if any(proc.poll() is not None for proc in processes):
                        raise AssertionError('A service exited: '+description)
                    time.sleep(.1)
                raise AssertionError('Timed out: '+description)

            admin, reader = token(['stats', 'control']), token(['stats'])
            eventually(lambda: request('/api/v1/session', admin)[0] == 200, 'web startup / APISync')
            status, payload = request('/api/v1/session', admin)
            assert json.loads(payload)['csrfToken'] == 'test-csrf-id'
            for jwt in (None, admin+'bad', token(['stats'], app='OTHER'), token(['stats'], expired=True)):
                assert request('/api/v1/session', jwt)[0] == 403
            assert request('/api/v1/alerts/stats', token([]))[0] == 401
            assert request('/api/v1/alerts/stats', reader)[0] == 503
            assert request('/api/v1/alerts/rules/save', reader, 'POST', {}, 'test-csrf-id')[0] == 401
            assert request('/api/v1/alerts/rules/save', admin, 'POST', {})[0] == 403
            assert request('/api/v1/not-a-resource', admin)[0] == 404
            assert request('/resources.conf', admin)[0] in (403,404)
            status, page = request('/mods/alerts/rules.html', admin)
            assert status == 200 and '<!doctype html>' in page and '<%include:' not in page and '{{' not in page, (status, page[:400])
            assert 'Welcome, test-user' in page and 'ruleCmd(' in page
            assert '>Remove</a>' in page and '/assets/js/jquery.min.js' in page
            assert '/auth/assets/js/auth.js' in page
            assert 'cdn.jsdelivr.net' not in page and 'code.jquery.com' not in page
            for path in ['/mods/alerts/rules.html', '/mods/alerts/rules', '/mods/alerts/']:
                status, page = request(path, reader)
                assert status == 200 and '{{' not in page, (path, status, page[:400])
                assert '>Remove</a>' not in page and '>Add New...</a>' not in page
            for path in ['/includes/layout/inc.header.html', '/includes/alerts/inc.links.html']:
                assert request(path, admin)[0] in (403, 404), path
            for path in ['/mods/alerts/rule-add.html', '/mods/alerts/action-add', '/mods/alerts/input-add.html']:
                assert request(path, reader)[0] == 403, path
                status, page = request(path, admin)
                assert status == 200 and '{{' not in page and '<form>' in page, (path, status)
            assert request('/mods/alerts/stats.html', token([]))[0] == 403
            status, page = request('/mods/alerts/rules.html', token(['stats', 'controller']))
            assert status == 200 and '>Remove</a>' not in page
            assert request('/mods/alerts/rule-add.html', token(['stats', 'controller']))[0] == 403
            for path in ['/', '/index.html', '/mods/log2json/eventsph.html']:
                status, page = request(path, admin)
                assert status == 200 and '{{' not in page and 'Welcome, test-user' in page, (path, status)
            for path in ['/index.html', '/assets/js/jquery.min.js', '/mods/alerts/rules.html']:
                assert request(path)[0] == 302, path
            for path in ['/assets/js/jquery.min.js', '/assets/css/bootstrap.min.css', '/assets/js/bootstrap.bundle.min.js']:
                status, asset = request(path, reader)
                assert status == 200 and len(asset) > 1000, (path, status)
            for path in ['/package.json', '/node_modules/jquery/dist/jquery.min.js']:
                assert request(path, admin)[0] in (403, 404), path

            status, page = request('/index.html', token(['stats'], subject='<img src=x onerror=alert(1)>'))
            assert status == 200 and '<img src=x' not in page and '&lt;img' in page
            # Check every rendered inline script, including controls emitted for each scope set.
            for page_file in (ROOT/'ul3_web/var/www/ulogeventaudit3/webroot/mods').glob('*/*.html'):
                path = '/'+str(page_file.relative_to(ROOT/'ul3_web/var/www/ulogeventaudit3/webroot'))
                for jwt in (admin, reader):
                    status, page = request(path, jwt)
                    if status == 403:
                        assert page_file.stem in ('rule-add', 'action-add', 'input-add') and jwt == reader
                        continue
                    assert status == 200 and '{{' not in page, (path, status)
                    scripts = re.findall(r'<script>(.*?)</script>', page, re.S)
                    if shutil.which('node'):
                        subprocess.run(['node', '--check'], input='\n'.join(scripts), text=True,
                                       capture_output=True, check=True)

            # Start actual alert and converter backends with empty temporary configurations.
            for name, key_name, key in [('ul3_alert', 'AlertsApiKey', 'integration-alert-key'),
                                        ('ul3_log2json', 'Log2JSONApiKey', 'integration-log-key')]:
                directory = tmp/name
                directory.mkdir()
                shutil.copy(tmp/'server.crt', directory/'rpcca.crt')
                ini = (ROOT/name/'etc/ulogeventaudit3'/name/'config.ini').read_text()
                ini = ini.replace('RemoteAddr=audit.localhost', 'RemoteAddr=localhost')
                ini = ini.replace('RemotePort=33001', f'RemotePort={rpc_port}')
                ini = re.sub(key_name+'=.*', key_name+'='+key, ini)
                ini = ini.replace('ToSyslog=true', 'ToSyslog=false')
                (directory/'config.ini').write_text(ini)
                (directory/'config.ini').chmod(0o600)
                if name == 'ul3_alert':
                    for file in ['rules.json', 'actions.json']:
                        (directory/file).write_text('[]')
                    (directory/'inputs.json').write_text(json.dumps([{'Description':'test input', 'ListenAddr':'127.0.0.1', 'ListenPort':alert_port, 'id':'test-input'}]))
                else:
                    (directory/'receptors.d').mkdir()
                    (directory/'dispatchers.d').mkdir()
                    (directory/'receptors.d/test.ini').write_text(f'ListenAddr=127.0.0.1\nListenPort={input_port}\n')
                    (directory/'dispatchers.d/test.ini').write_text(f'Host=127.0.0.1\nPort={alert_port}\nTransmitionMode=0\n')
                launch(name, directory)
            eventually(lambda: request('/api/v1/alerts/stats', reader)[0] == 200, 'alert backend TLS / FastRPC1')
            eventually(lambda: request('/api/v1/log2json/inputs', reader)[0] == 200, 'converter TLS / FastRPC1')
            # Numeric GET arguments must survive encoded JSON and FastRPC1.
            for path, member in [('alerts/rules', None), ('alerts/actions', None),
                                 ('log2json/hosts', 'hosts'), ('log2json/inputs', 'inputs'),
                                 ('log2json/outputs', 'outputs')]:
                status, body = request('/api/v1/'+path, reader)
                result = json.loads(body)
                assert status == 200 and isinstance(result if member is None else result[member], list), (path, body)
            # Input configuration writes preserve Unicode, escapes, numbers and booleans.
            new_input = {'Description': 'test \u03bb "quoted"\ninput', 'ListenAddr': '127.0.0.1',
                         'ListenPort': 12345, 'enabled': False}
            status, body = request('/api/v1/alerts/inputs/config', admin, 'POST', new_input, 'test-csrf-id')
            assert status == 200 and json.loads(body) is True, (status, body)
            status, body = request('/api/v1/alerts/inputs/config', reader)
            added_input = json.loads(body)['inputs'][0]
            assert all(added_input[key] == value for key, value in new_input.items()), body
            status, body = request('/api/v1/alerts/inputs/config', admin, 'DELETE',
                                   {'id': added_input['id']}, 'test-csrf-id')
            assert status == 200 and json.loads(body) is True, (status, body)
            assert len(json.loads((tmp/'ul3_alert/inputs.json').read_text())) == 1
            input_file = tmp/'ul3_alert/inputs.json'
            original_inputs = input_file.read_text()
            status, body = request('/api/v1/alerts/inputs/config', admin, 'DELETE',
                                   {'id': 'test-input'}, 'test-csrf-id')
            assert status == 200 and json.loads(body) is True, (status, body)
            assert json.loads(input_file.read_text()) == []
            input_file.write_text('null')
            status, body = request('/api/v1/alerts/inputs/config', reader)
            assert status == 200 and json.loads(body)['inputs'] == [], (status, body)
            for invalid_document in ['{', '{}', '[] trailing']:
                input_file.write_text(invalid_document)
                status, body = request('/api/v1/alerts/inputs/config', admin, 'POST', new_input, 'test-csrf-id')
                assert status == 200 and json.loads(body) is False, (status, body)
                assert input_file.read_text() == invalid_document
            input_file.write_text(original_inputs)
            for resource in ['actions', 'rules']:
                status, body = request('/api/v1/alerts/'+resource+'/save', admin, 'POST', {}, 'test-csrf-id')
                assert status == 200 and json.loads(body) is True, (status, body)
                config_file = tmp/'ul3_alert'/(resource+'.json')
                assert json.loads(config_file.read_text()) == []
                status, body = request('/api/v1/alerts/'+resource+'/config', admin, 'PUT', {}, 'test-csrf-id')
                assert status == 200 and json.loads(body) is False, (status, body)
                assert json.loads(config_file.read_text()) == []
            assert request('/api/v1/alerts/rules/item', admin, data={'id': 0})[0] == 200
            action = {'name': 'test-action', 'description': 'test', 'path': '/bin/true', 'args': ['/bin/true']}
            status, body = request('/api/v1/alerts/actions', admin, 'POST', action, 'test-csrf-id')
            assert status == 200 and json.loads(body) is True, (status, body)
            status, body = request('/api/v1/alerts/actions/item', admin, data={'name': 'test-action'})
            assert status == 200 and json.loads(body)['name'] == 'test-action', (status, body)
            assert request('/api/v1/alerts/actions', admin, 'DELETE', {'name': 'test-action'}, 'test-csrf-id')[0] == 200
            marker = tmp/'rule-executed'
            capture = 'import json,sys; from pathlib import Path; Path(sys.argv[1]).write_text(json.dumps(sys.argv[2:]))'
            action = {'name': 'pipeline-action', 'path': sys.executable,
                      'args': [sys.executable, '-c', capture, str(marker),
                               '%-.INFO.id%', '%-.CWD[0].cwd%', '%+.CWD[0].cwd%',
                               '%$.CWD[0]%', '%-.missing[0]%', '%#rule.name%', '%-.EXECVE[0].argv%',
                               '%%/%#rule.name%/%#rule.name%/%N%/tail%', '%-.CWD[0].literal%']}
            status, body = request('/api/v1/alerts/actions', admin, 'POST', action, 'test-csrf-id')
            assert status == 200 and json.loads(body) is True, (status, body)
            rule = {'name': 'pipeline-rule', 'action': 'EXEC', 'actionId': 'pipeline-action',
                    'filter': 'IS_EQUAL($.CWD[0].cwd,"/tmp")'}
            status, body = request('/api/v1/alerts/rules', admin, 'POST', {'pos': 0, 'rule': rule}, 'test-csrf-id')
            assert status == 200 and json.loads(body) is True, (status, body)
            # Replacements and reordering must retain ownership and the configured values.
            action['description'] = 'updated pipeline action'
            status, body = request('/api/v1/alerts/actions', admin, 'PUT', action, 'test-csrf-id')
            assert status == 200 and json.loads(body) is True, (status, body)
            status, body = request('/api/v1/alerts/rules', admin, 'PUT', {'pos': 0, 'rule': rule}, 'test-csrf-id')
            assert status == 200 and json.loads(body) is True, (status, body)
            spare_rule = dict(rule, name='spare-rule')
            status, body = request('/api/v1/alerts/rules', admin, 'POST', {'pos': 1, 'rule': spare_rule}, 'test-csrf-id')
            assert status == 200 and json.loads(body) is True, (status, body)
            for direction, position in [('up', 1), ('down', 0)]:
                status, body = request('/api/v1/alerts/rules/' + direction, admin, 'POST', {'pos': position}, 'test-csrf-id')
                assert status == 200 and json.loads(body) is True, (status, body)
            status, body = request('/api/v1/alerts/rules/item', reader, data={'id': 0})
            assert status == 200 and json.loads(body)['name'] == rule['name'], (status, body)
            status, body = request('/api/v1/alerts/rules', admin, 'DELETE', {'pos': 1}, 'test-csrf-id')
            assert status == 200 and json.loads(body) is True, (status, body)
            # Invalid numeric positions must never wrap or truncate to rule zero.
            for invalid_id in [-1, 0.5, 1 << 32, '0', None]:
                status, body = request('/api/v1/alerts/rules/item', reader, data={'id': invalid_id})
                assert status == 200 and json.loads(body) is None, (status, body)
                status, body = request('/api/v1/alerts/rules', admin, 'DELETE',
                                       {'pos': invalid_id}, 'test-csrf-id')
                assert status == 200 and json.loads(body) is False, (status, body)
            with socket.create_connection(('127.0.0.1', input_port), timeout=5) as incoming:
                event_id = (1 << 64) - 1
                stamp = f'{int(time.time())}.123:{event_id}'
                lines = [f'node=integration type=CWD msg=audit({stamp}): cwd="/tmp" literal="%N%"',
                         f'<14>Jun 22 12:19:47 relay audispd: node=integration type=EXECVE msg=audit({stamp}): argc=2 a0[0]=6563',
                         f'Jun 22 12:19:47 relay audisp-syslog[673]: node=integration type=EXECVE msg=audit({stamp}): a0[1]=686F a1="hello"']
                incoming.sendall(('\n'.join(lines)+'\n').encode())
            eventually(marker.exists, 'audit input -> reconstructed JSON -> TCP output -> expression -> action')
            captured = json.loads(marker.read_text())
            assert captured[0] == str(event_id), captured
            assert json.loads(captured[1]) == '/tmp', captured
            assert json.loads(captured[2]) == '/TMP', captured
            assert json.loads(captured[3]) == {'cwd': '/tmp', 'literal': '%N%'} and captured[3].endswith('\n'), captured
            assert captured[4:6] == ['null', 'pipeline-rule'], captured
            assert json.loads(captured[6]) == ['echo', 'hello'], captured
            assert captured[7] == '%/pipeline-rule/pipeline-rule/\n/tail%', captured
            assert json.loads(captured[8]) == '%N%', captured
            for resource in ['actions', 'rules']:
                status, body = request('/api/v1/alerts/'+resource+'/save', admin, 'POST', {}, 'test-csrf-id')
                assert status == 200 and json.loads(body) is True, (status, body)
                assert json.loads((tmp/'ul3_alert'/ (resource+'.json')).read_text())
            assert any(name == 'updateAccessControlContext'  for name, _ in AppSync.requests)
            print('PASS: JWT, scopes, CSRF, resources, APISync, TLS, both FastRPC1 backends, GET parameters, persisted writes and audit-to-action pipeline')
        except Exception:
            for log in logfiles:
                log.flush()
                print('\n--- '+log.name+' ---\n'+Path(log.name).read_text()[-9000:])
            raise
        finally:
            for proc in reversed(processes):
                proc.terminate()
            for proc in processes:
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
            for log in logfiles:
                log.close()
            server.shutdown()


if __name__ == '__main__':
    main()

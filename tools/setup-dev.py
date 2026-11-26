#!/usr/bin/env python3
"""Install an isolated development configuration; preserve existing local settings."""
import argparse
import base64
import json
from pathlib import Path
import secrets
import shlex
import shutil
import subprocess


def write_new(path, content, mode=0o600):
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open('x') as stream:
            stream.write(content)
        path.chmod(mode)
    except FileExistsError:
        pass


def link_new(path, target):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink() and path.resolve() == target.resolve():
        return
    if path.exists() or path.is_symlink():
        raise RuntimeError(f'Refusing to replace {path}')
    path.symlink_to(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, default=Path.home() / 'Devel/ROOT')
    parser.add_argument('--build-dir', type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    prefix = args.prefix.resolve()
    build = (args.build_dir or repo / 'build/devel-root').resolve()
    config = prefix / 'etc/ulogeventaudit3'
    web = config / 'ul3_web'
    logs = prefix / 'var/log/ulogeventaudit3'
    logs.mkdir(parents=True, exist_ok=True)
    web.mkdir(parents=True, exist_ok=True)
    # Store secrets outside the checkout and reuse them on subsequent runs.
    write_new(config / 'dev-secrets.json', json.dumps({
        'alerts': secrets.token_urlsafe(48),
        'log2json': secrets.token_urlsafe(48),
    }, indent=2) + '\n')
    keys = json.loads((config / 'dev-secrets.json').read_text())
    if not (web / 'snakeoil.crt').exists() and not (web / 'snakeoil.key').exists():
        subprocess.run([
            'openssl', 'req', '-x509', '-newkey', 'rsa:3072', '-nodes',
            '-keyout', str(web / 'snakeoil.key'), '-out', str(web / 'snakeoil.crt'),
            '-days', '825', '-subj', '/CN=localhost',
            '-addext', 'subjectAltName=DNS:localhost,DNS:audit.localhost,IP:127.0.0.1',
            '-addext', 'basicConstraints=critical,CA:TRUE',
        ], check=True, capture_output=True)
    if not (web / 'snakeoil.key').exists() or not (web / 'snakeoil.crt').exists():
        raise RuntimeError('Incomplete TLS key/certificate pair')
    (web / 'snakeoil.key').chmod(0o600)
    auth_ca = prefix / 'etc/ufastauthd3/tls/ca.crt'
    if not (web / 'ca.crt').exists():
        shutil.copyfile(auth_ca, web / 'ca.crt')
    url = 'https://localhost:33100'
    text = (repo / 'ul3_web/etc/ulogeventaudit3/ul3_web/webserver.conf').read_text()
    for old, new in {
        'REPLACEME_XABCXAPIX_ALERTS': keys['alerts'],
        'REPLACEME_XABCXAPIX_LOG2JSON': keys['log2json'],
        '/var/log/ulogeventaudit3': str(logs),
        '/var/www/ulogeventaudit3': str(prefix / 'var/www/ulogeventaudit3'),
        '33000': '33100', '33001': '33101',
        'https://audit.localhost:33100': url,
        'aHR0cHM6Ly9hdWRpdC5sb2NhbGhvc3Q6MzMwMDAv': base64.b64encode((url + '/').encode()).decode(),
        'ListenAddr "0.0.0.0"': 'ListenAddr "127.0.0.1"',
    }.items():
        text = text.replace(old, new)
    write_new(web / 'webserver.conf', text)
    for name, key in [('ul3_alert', 'alerts'), ('ul3_log2json', 'log2json')]:
        target = config / name
        source = repo / name / 'etc/ulogeventaudit3' / name
        text = (source / 'config.ini').read_text()
        text = text.replace('33001', '33101').replace('RemoteAddr=audit.localhost', 'RemoteAddr=localhost')
        text = text.replace('REPLACEME_XABCXAPIX_ALERTS', keys[key]).replace('REPLACEME_XABCXAPIX_LOG2JSON', keys[key])
        write_new(target / 'config.ini', text)
        write_new(target / 'rpcca.crt', (web / 'snakeoil.crt').read_text(), 0o644)
    alert = config / 'ul3_alert'
    write_new(alert / 'inputs.json', json.dumps([{
        'Description': 'Local development JSON input', 'ListenAddr': '127.0.0.1',
        'ListenPort': 19999, 'id': 'dev-local',
    }], indent=2) + '\n')
    write_new(alert / 'rules.json', '[]\n')
    write_new(alert / 'actions.json', '[]\n')
    converter = config / 'ul3_log2json'
    source = repo / 'ul3_log2json/etc/ulogeventaudit3/ul3_log2json'
    inputs = json.loads((source / 'inputs.json').read_text())
    for endpoint in inputs:
        endpoint['ListenAddr'] = '127.0.0.1'
        endpoint['ListenPort'] = 11514
    write_new(converter / 'inputs.json', json.dumps(inputs, indent=2) + '\n')
    outputs = [endpoint for endpoint in json.loads((source / 'outputs.json').read_text())
               if endpoint['id'] == 'alerts']
    outputs[0]['Port'] = 19999
    write_new(converter / 'outputs.json', json.dumps(outputs, indent=2) + '\n')
    link_new(prefix / 'var/www/ulogeventaudit3', repo / 'ul3_web/var/www/ulogeventaudit3')
    for name in ['ul3_web', 'ul3_alert', 'ul3_log2json']:
        binary = build / name / name
        if not binary.is_file():
            raise RuntimeError(f'Build first: missing {binary}')
        link_new(prefix / 'bin' / name, binary)
        launcher = prefix / 'bin' / (name + '-dev')
        write_new(launcher, '#!/bin/bash\nset -e\n'
                  + f'export LD_LIBRARY_PATH={shlex.quote(str(prefix / "lib"))}${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}\n'
                  + f'exec {shlex.quote(str(prefix / "bin" / name))} -c {shlex.quote(str(config / name))} "$@"\n', 0o755)
        write_new(prefix / 'lib/systemd/user' / (name + '-dev.service'),
                  f'[Unit]\nDescription=uLogEventAudit3 development {name}\n\n'
                  f'[Service]\nExecStart={shlex.quote(str(launcher))}\n'
                  + ('Restart=always\n' if name == 'ul3_log2json' else 'Restart=on-failure\n')
                  + 'RestartSec=5\n\n'
                  '[Install]\nWantedBy=default.target\n', 0o644)
    write_new(config / 'README.txt', f'''Instalación local de desarrollo de uLogEventAudit3

Web: {url}/ (solo loopback)
RPC TLS: localhost:33101
Entrada syslog auditd TCP: 127.0.0.1:11514
Entrada JSON alertas TCP: 127.0.0.1:19999
Web enlazada al repositorio: {prefix}/var/www/ulogeventaudit3
Logs web: {logs}
Certificado local para importar en el navegador: {web}/snakeoil.crt

Arrancar en tres terminales:
  {prefix}/bin/ul3_web-dev
  {prefix}/bin/ul3_alert-dev
  {prefix}/bin/ul3_log2json-dev
Detener con Ctrl+C. También hay unidades opcionales en {prefix}/lib/systemd/user.

Login pendiente: registrar ULOGEVENTAUDIT3 en uFastAuthD3 (https://iamadmin.localhost:9443),
permitir origen {url}, redirección {url}/ y callback {url}/api/v1/callback.
Guardar su API key en Authentication.APIKey de {web}/webserver.conf (modo 0600),
o exportar UL3_API_KEY antes de arrancar la web. Asignar AUDIT_ADMIN al usuario de pruebas.
APISync: login.localhost:7081; proxy auth: login.localhost:7080; portal: 8443.
Se conserva la CA de tu instalación uFastAuthD3. Su certificado actual declara DNS:snakeoil;
debe renovarse para login.localhost si la conexión rechaza su identidad TLS.

Las reglas y acciones empiezan vacías; el único dispatcher envía al motor de alertas local.
Volver a ejecutar tools/setup-dev.py conserva configuraciones, claves y certificados existentes.
Recompilar: cmake --build {build} -j 4
''', 0o644)
    print(f'Instalación preparada en {prefix}; web: {url}/')


if __name__ == '__main__':
    main()

"""Check real HTTP POST delivery, failures and redirect behavior on loopback only."""
import http.server
import json
import subprocess
import sys
import threading


class Handler(http.server.BaseHTTPRequestHandler):
    """Record requests and return controlled HTTP outcomes."""

    records = []

    def do_POST(self):
        """Capture the rendered JSON body and simulate the requested status."""
        content = self.rfile.read(int(self.headers['Content-Length']))
        self.records.append((self.path, self.headers['Content-Type'], json.loads(content)))
        code = {'/ok': 204, '/error': 500, '/redirect': 302, '/truncated': 200}[self.path]
        self.send_response(code)
        self.send_header('Content-Length', '100' if self.path == '/truncated' else '0')
        if self.path == '/redirect':
            self.send_header('Location', '/ok')
        self.end_headers()

    def log_message(self, *_):
        """Keep expected HTTP errors out of test output."""


with http.server.HTTPServer(('127.0.0.1', 0), Handler) as server:
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        for path, expected in [('/ok', 0), ('/error', 1), ('/redirect', 1), ('/truncated', 1)]:
            result = subprocess.run([sys.argv[1], '--post-url', f'http://127.0.0.1:{server.server_port}{path}'], timeout=10)
            assert result.returncode == expected, (path, result.returncode)
        assert len(Handler.records) == 4, 'A redirect was followed or a delivery was retried'
        for path, content_type, body in Handler.records:
            assert content_type == 'application/json', content_type
            assert body == {'message': 'Quotes "\\\n λ'}, body
    finally:
        server.shutdown()
        worker.join()
print('PASS: real HTTP POST body, status errors, redirects and truncated responses')

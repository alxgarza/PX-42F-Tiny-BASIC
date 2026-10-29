#!/usr/bin/env python3
"""The live simulator: a web page for build/sim --live.

    python3 sim/live.py SIM DRIVE_TEMPLATE DRIVE_RUN [REBUILD_COMMAND]

Serves sim/live.html on http://127.0.0.1:8042 (another port if that one is
taken), runs SIM with a fresh copy of DRIVE_TEMPLATE in DRIVE_RUN, sends the
screen and the console to the page, and the page's keys to SIM. The page's
Restart button runs REBUILD_COMMAND, copies the drive again and starts SIM
again. Only this computer can reach it. Stop it with Ctrl-C.
"""
import base64
import http.server
import json
import mimetypes
import os
import queue
import shutil
import subprocess
import sys
import threading
import urllib.parse
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 8042
LOG_KEPT = 400


class Session:
    def __init__(self, sim, template, drive, rebuild):
        self.sim, self.template, self.drive, self.rebuild = sim, template, drive, rebuild
        self.proc = None
        self.info = None
        self.frame = None
        self.log = []
        self.state = {'state': 'starting'}
        self.clients = []
        self.lock = threading.Lock()
        self.restarting = threading.Lock()

    def send(self, proc, event, data):
        with self.lock:
            if proc is not self.proc and proc is not None:
                return
            if event == 'frame':
                self.frame = data
            elif event == 'log':
                self.log = (self.log + [data])[-LOG_KEPT:]
            elif event == 'hello':
                self.info = data
            elif event == 'status':
                self.state = data
            for c in self.clients:
                c.put((event, data))

    def snapshot(self):
        with self.lock:
            first = [('status', self.state)]
            if self.info:
                first.append(('hello', self.info))
            first += [('log', t) for t in self.log]
            if self.frame:
                first.append(('frame', self.frame))
            q = queue.Queue()
            for item in first:
                q.put(item)
            self.clients.append(q)
            return q

    def leave(self, q):
        with self.lock:
            if q in self.clients:
                self.clients.remove(q)

    def restart(self):
        if not self.restarting.acquire(blocking=False):
            return
        try:
            self.stop()
            self.send(None, 'status', {'state': 'building'})
            if self.rebuild:
                done = subprocess.run(self.rebuild, shell=True, capture_output=True, text=True)
                self.send(None, 'log', done.stdout + done.stderr)
                if done.returncode != 0:
                    self.send(None, 'status', {'state': 'build failed'})
                    return
            self.start()
        finally:
            self.restarting.release()

    def start(self):
        shutil.rmtree(self.drive, ignore_errors=True)
        shutil.copytree(self.template, self.drive)
        r, w = os.pipe()
        proc = subprocess.Popen([self.sim, '--live', self.drive, str(w)], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, pass_fds=(w,))
        os.close(w)
        with self.lock:
            self.proc, self.info, self.frame, self.log = proc, None, None, []
        self.send(proc, 'status', {'state': 'running'})
        threading.Thread(target=self.read_frames, args=(proc, r), daemon=True).start()
        threading.Thread(target=self.read_console, args=(proc,), daemon=True).start()
        threading.Thread(target=self.wait, args=(proc,), daemon=True).start()

    def stop(self):
        with self.lock:
            proc, self.proc = self.proc, None  # what it says from now on isn't passed on
        if proc is not None and proc.poll() is None:
            proc.kill()
            proc.wait()

    def read_frames(self, proc, fd):
        with os.fdopen(fd, 'rb') as f:
            width, height, stride = map(int, f.readline().split() or [0, 0, 0])
            if not width:
                return
            self.send(proc, 'hello', {'w': width, 'h': height, 'stride': stride})
            size = height * stride
            while True:
                tag = f.read(1)
                if tag == b'F':
                    data = f.read(size)
                    if len(data) < size:
                        return
                    self.send(proc, 'frame', base64.b64encode(data).decode())
                elif tag == b'S':
                    self.send(proc, 'sound', f.readline().decode('utf-8', 'replace').strip())
                elif tag == b'X':
                    self.send(proc, 'sound', '')
                else:
                    return

    def read_console(self, proc):
        while True:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                return
            self.send(proc, 'log', chunk.decode('utf-8', 'replace'))

    def wait(self, proc):
        code = proc.wait()
        self.send(proc, 'status', {'state': 'ended', 'code': code})

    def input(self, text):
        proc = self.proc
        if proc is None or proc.poll() is not None:
            return
        try:
            proc.stdin.write(text.encode())
            proc.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass


class Handler(http.server.BaseHTTPRequestHandler):
    session = None

    def log_message(self, *args):
        pass

    def local_origin(self):
        origin = self.headers.get('Origin')
        host = self.headers.get('Host', '')
        return host.split(':')[0] in ('127.0.0.1', 'localhost') and (
            origin is None or origin.split('//')[-1] == host)

    def do_GET(self):
        if not self.local_origin():
            self.send_error(403)
        elif self.path == '/events':
            self.events()
        elif self.path.startswith('/drive/'):
            self.drive_file(urllib.parse.unquote(self.path[len('/drive/'):]))
        elif self.path == '/':
            page = open(os.path.join(HERE, 'live.html'), 'rb').read()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(page)))
            self.end_headers()
            self.wfile.write(page)
        else:
            self.send_error(404)

    def do_POST(self):
        if not self.local_origin():
            return self.send_error(403)
        body = self.rfile.read(int(self.headers.get('Content-Length', 0))).decode('utf-8', 'replace')
        if self.path == '/input':
            self.session.input(body)
        elif self.path == '/restart':
            threading.Thread(target=self.session.restart, daemon=True).start()
        else:
            return self.send_error(404)
        self.send_response(204)
        self.end_headers()

    def drive_file(self, rel):
        root = os.path.realpath(self.session.drive)
        path = os.path.realpath(os.path.join(root, rel))
        if not path.startswith(root + os.sep) or not os.path.isfile(path):
            return self.send_error(404)
        data = open(path, 'rb').read()
        self.send_response(200)
        self.send_header('Content-Type', mimetypes.guess_type(path)[0] or 'application/octet-stream')
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(data)

    def events(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.send_header('Cache-Control', 'no-cache')
        self.end_headers()
        q = self.session.snapshot()
        try:
            while True:
                try:
                    event, data = q.get(timeout=15)
                    self.wfile.write(f'event: {event}\ndata: {json.dumps(data)}\n\n'.encode())
                except queue.Empty:
                    self.wfile.write(b': keep-alive\n\n')
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            self.session.leave(q)


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    session = Session(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else None)
    Handler.session = session
    try:
        server = http.server.ThreadingHTTPServer(('127.0.0.1', PORT), Handler)
    except OSError:
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    url = 'http://127.0.0.1:%d/' % server.server_address[1]
    session.start()
    print('The live simulator is at', url, '(Ctrl-C to stop)', flush=True)
    webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        session.stop()


main()

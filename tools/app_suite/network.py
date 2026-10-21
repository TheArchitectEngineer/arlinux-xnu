"""Bounded real HTTP peer owned by one network app journey."""
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import json
import threading
class HttpFixture:
    def __init__(self,directory):
        self.requests=[];self.directory=directory
        owner=self
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                owner.requests.append({'path':self.path,'peer':self.client_address[0]})
                body=b'{"answer":42}' if self.path=='/value.json' else b'{}'
                self.send_response(200 if self.path=='/value.json' else 404)
                self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)))
                self.send_header('Cache-Control','no-store')
                self.end_headers();self.wfile.write(body)
            def log_message(self,*args): pass
        self.server=ThreadingHTTPServer(('127.0.0.1',18765),Handler)
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True)
    def start(self):self.thread.start()
    def received(self,minimum=1):return sum(item['path']=='/value.json' for item in self.requests)>=minimum
    def stop(self):
        self.server.shutdown();self.server.server_close();self.thread.join(timeout=3)
        (self.directory/'http-requests.json').write_text(json.dumps(self.requests,indent=2)+'\n')

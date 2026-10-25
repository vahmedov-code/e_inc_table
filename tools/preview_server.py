"""UI-only simulator. Does not connect to hardware. Log in with «Войти по ключу API», key: preview. Binds loopback."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import time

root=Path(__file__).resolve().parents[1]
state={'version':'0.2.0','desired':'closed','displayed':'closed','busy':False,
       'displayError':'','hardwareConfirmed':False,'storageReady':False,
       'automatic':False,'source':'manual','ip':'192.168.1.42','apIP':'192.168.4.1',
       'wifiConnected':True,'timeValid':True,'cooldownSeconds':0,'assets':{}}
schedule={'enabled':False,'utcOffsetMinutes':180,'events':[]}
frames={}
now=int(time.time())
events=[{'seq':6,'time':now-60,'timeValid':True,'boot':3,'kind':'refresh','code':0,'text':'Экран обновлён: «Открыто» за 24,8 с, АКБ 3,98 В'},
        {'seq':5,'time':now-900,'timeValid':True,'boot':3,'kind':'wifi_up','code':-63,'text':'Wi-Fi вернулся через 2 мин 10 с, сигнал -63 дБм'},
        {'seq':4,'time':now-1030,'timeValid':True,'boot':3,'kind':'wifi_down','code':200,'text':'Wi-Fi пропал: пропал сигнал роутера (код 200), сигнал был -79 дБм'},
        {'seq':3,'time':now-3600,'timeValid':True,'boot':3,'kind':'wifi_up','code':-61,'text':'Подключилась к Wi-Fi через 6 с после запуска, сигнал -61 дБм'},
        {'seq':2,'time':0,'timeValid':False,'boot':3,'kind':'boot','code':9,'text':'Запуск 0.4.0 · просадка питания (brownout)'},
        {'seq':1,'time':now-3700,'timeValid':True,'boot':2,'kind':'before_reset','code':3712,'text':'Перед сбросом: работала 2 ч 3 мин, АКБ 3,71 В, мин. память 44 КБ, шло обновление экрана'}]

def builtin(slot):
    # The firmware's built-in templates, packed like tools/make_templates.py does (needs Pillow).
    try:
        from make_templates import pack
        return pack(root/'assets/templates'/f'{slot}.png')
    except Exception:
        return None

class Handler(BaseHTTPRequestHandler):
    def send(self,code,obj):
        b=json.dumps(obj).encode();self.send_response(code)
        self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(b)))
        self.end_headers();self.wfile.write(b)
    def log_message(self,*args): pass
    def do_GET(self):
        if self.path=='/':
            b=(root/'web/index.html').read_bytes();self.send_response(200)
            self.send_header('Content-Type','text/html; charset=utf-8');self.end_headers();self.wfile.write(b);return
        if self.path=='/api/v1/auth':self.send(200,{'setup':False,'setupAllowed':False,'loggedIn':self.headers.get('Authorization')=='Bearer preview','user':'admin'});return
        if self.headers.get('Authorization')!='Bearer preview':self.send(401,{'error':'Введите тестовый ключ preview'});return
        if self.path=='/api/v1/state':self.send(200,{**state,'unixTime':int(time.time())})
        elif self.path=='/api/v1/schedule':self.send(200,schedule)
        elif self.path=='/api/v1/events':self.send(200,{'next':7,'boot':3,'now':int(time.time()),'uptime':3600,'capacity':64,'events':events})
        elif self.path.startswith('/api/v1/builtin?slot=') and builtin(self.path.split('=')[1]):
            b=builtin(self.path.split('=')[1]);self.send_response(200)
            self.send_header('Content-Type','application/octet-stream');self.send_header('Content-Length',str(len(b)))
            self.end_headers();self.wfile.write(b)
        elif self.path.startswith('/api/v1/asset?slot=') and self.path.split('=')[1] in frames:
            b=frames[self.path.split('=')[1]];self.send_response(200)
            self.send_header('Content-Type','application/octet-stream');self.send_header('Content-Length',str(len(b)))
            self.end_headers();self.wfile.write(b)
        else:self.send(404,{'error':'Нет сохранённого изображения в симуляторе'})
    def do_POST(self):
        if self.path.startswith('/api/v1/assets?slot='):self.store();return
        self.mutate()
    def do_DELETE(self):
        if self.headers.get('Authorization')!='Bearer preview':self.send(401,{'error':'Нужен тестовый ключ preview'});return
        slot=self.path.split('=')[-1];frames.pop(slot,None);state['assets'][slot]=False;self.send(200,state)
    def store(self):
        if self.headers.get('Authorization')!='Bearer preview':self.send(401,{'error':'Нужен тестовый ключ preview'});return
        if not state['storageReady']:self.send(400,{'error':'Хранилище не подготовлено'});return
        b=self.rfile.read(int(self.headers.get('Content-Length',0)))
        boundary=self.headers.get('Content-Type','').split('boundary=')[-1].encode()
        part=b.split(b'--'+boundary)[1];frame=part.split(b'\r\n\r\n',1)[1][:-2]
        if len(frame)!=96000:self.send(400,{'error':'Нужен файл ровно 96000 байт'});return
        slot=self.path.split('=')[-1];frames[slot]=frame;state['assets'][slot]=True;self.send(200,state)
    def do_PUT(self): self.mutate()
    def mutate(self):
        if self.headers.get('Authorization')!='Bearer preview':self.send(401,{'error':'Нужен тестовый ключ preview'});return
        b=self.rfile.read(int(self.headers.get('Content-Length',0)))
        try:d=json.loads(b)
        except Exception:self.send(400,{'error':'Симулятор поддерживает только JSON-действия'});return
        if self.path=='/api/v1/status':state.update(desired=d['status'],displayed=d['status'],automatic=False)
        elif self.path=='/api/v1/schedule':schedule.update(d);state['automatic']=d['enabled']
        elif self.path=='/api/v1/hardware':state['hardwareConfirmed']=True
        elif self.path=='/api/v1/storage/init':state['storageReady']=True
        self.send(200,state)

if __name__=='__main__':
    print('UI SIMULATOR ONLY: http://127.0.0.1:8765 / key: preview',flush=True)
    ThreadingHTTPServer(('127.0.0.1',8765),Handler).serve_forever()

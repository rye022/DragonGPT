"""Private JSON-lines tool host. No eval, shell strings, implicit grants or credentials.

The trusted Qt controller supplies a session root and a separate approval bit.
The model only supplies tool arguments validated by the registry in both layers.
"""
from __future__ import annotations
import ctypes
import hashlib
import html
import http.client
import ipaddress
import json
import os
from pathlib import Path
import re
import shutil
import socket
import ssl
import subprocess
import sys
import time
import urllib.parse
import urllib.request
import zipfile

MAX_TEXT = 24000
RISKY = {'file_write', 'file_replace', 'file_copy', 'file_move', 'file_mkdir',
         'file_trash', 'web_download', 'browser_open', 'browser_click', 'browser_fill',
         'browser_scroll', 'browser_upload', 'browser_download', 'browser_tab',
         'desktop_invoke', 'desktop_fill', 'desktop_focus', 'desktop_launch', 'desktop_key', 'desktop_click', 'dev_run'}
DEVELOPER = {'file_write', 'file_replace', 'dev_run'}
TEXT_EXT = {'.txt','.json','.csv','.md','.cpp','.cc','.c','.h','.hpp','.py','.js','.ts',
            '.tsx','.jsx','.html','.css','.xml','.yaml','.yml','.toml','.cmake','.log','.rst','.ps1'}
SENSITIVE = re.compile(r'(^|/)(\.git|\.ssh|\.aws|\.azure|\.codex|\.gnupg|User Data|Login Data|Cookies|Local State)(/|$)|'
                       r'(^|/)(\.env[^/]*|[^/]*\.(dpapi|pem|pfx|key|sqlite|sqlite3|db|ini))$', re.I)
SECRET = re.compile(r'\bsk-[A-Za-z0-9_-]{16,}|-----BEGIN .*PRIVATE KEY-----|'
                    r'(?:api[_ -]?key|access[_ -]?token|password|secret)\s*[=:]\s*[\"\']?[^\s\"\']{8,}', re.I)

def bounded_text(value):
    text = str(value)
    if SECRET.search(text):
        raise ValueError('Sensitive content blocked before model upload')
    return text[:MAX_TEXT]

def safe_path(root, value, *, allow_root=False):
    if not isinstance(value,str) or not value or len(value)>2048 or '\x00' in value:
        raise ValueError('Invalid path')
    normalized = value.replace('\\','/')
    if normalized.startswith('//') or ':' in normalized[2:] or ('..' in normalized.split('/')):
        raise ValueError('UNC, alternate streams and traversal denied')
    for component in normalized.split('/'):
        if component in ('','.'):continue
        if component.endswith((' ','.')) or re.fullmatch(r'(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?',component,re.I):
            raise ValueError('Win32 device names and ambiguous trailing dots/spaces denied')
    base = Path(root).resolve(strict=True)
    if not base.is_dir() or base == Path(base.anchor):
        raise ValueError('Choose a normal project directory')
    for env in ('SystemRoot','ProgramFiles','ProgramFiles(x86)','ProgramData','LOCALAPPDATA','APPDATA'):
        protected = os.environ.get(env)
        if protected and base.is_relative_to(Path(protected).resolve()):
            raise ValueError('Protected workspace denied')
    raw = Path(value)
    raw = raw if raw.is_absolute() else base / raw
    if not raw.is_relative_to(base):
        raise ValueError('Path outside authorized workspace')
    for p in [raw, *raw.parents]:
        if p.is_symlink() or (hasattr(p,'is_junction') and p.is_junction()):
            raise ValueError('Links/reparse points denied')
        if os.name=='nt' and p.exists() and p.stat().st_file_attributes & 0x400:
            raise ValueError('Reparse points denied')
    resolved = raw.resolve()
    if not resolved.is_relative_to(base) or (resolved == base and not allow_root):
        raise ValueError('Path outside workspace or targets root')
    if SENSITIVE.search(resolved.relative_to(base).as_posix()):
        raise ValueError('Sensitive path denied')
    host_base=Path(__file__).resolve().parent
    if resolved.is_relative_to(host_base) or resolved.is_relative_to(host_base.parent/'asr') or resolved.name=='ready.json':
        raise ValueError('Tool host code/readiness cannot be accessed through model filesystem tools')
    return resolved

DNS_CACHE={}
def public_addresses(host,port):
    cached=DNS_CACHE.get(host)
    if cached and time.monotonic()-cached[0]<60:return cached[1]
    addresses=list(dict.fromkeys(x[4][0] for x in socket.getaddrinfo(host,port,type=socket.SOCK_STREAM)))
    # Some Windows TUN clients synthesize 198.18/15 or 2001:2/48 addresses.
    # Verify those names with certificate-validated public DoH, never waive the
    # private-IP check. Ordinary LAN/loopback resolutions still fail closed.
    fake=(ipaddress.ip_network('198.18.0.0/15'),ipaddress.ip_network('2001:2::/48'))
    if addresses and all(any(ipaddress.ip_address(ip) in net for net in fake if net.version==ipaddress.ip_address(ip).version) for ip in addresses):
        opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),NoRedirect())
        with opener.open('https://dns.google/resolve?type=A&name='+urllib.parse.quote(host,safe=''),timeout=10) as response:
            answer=json.loads(response.read(16384))
        addresses=[r['data'] for r in answer.get('Answer',[]) if r.get('type')==1]
    if not addresses or any(not ipaddress.ip_address(x).is_global for x in addresses):
        raise ValueError('Local/private network denied')
    DNS_CACHE[host]=(time.monotonic(),addresses)
    return addresses

def public_url(value):
    u = urllib.parse.urlsplit(value)
    if u.scheme not in ('http','https') or not u.hostname or u.username or u.password or u.port not in (None,80,443):
        raise ValueError('Only public HTTP(S) URLs without credentials are allowed')
    public_addresses(u.hostname,u.port or (443 if u.scheme=='https' else 80))
    return value

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs): return None

def tls_context():
    # Windows trust roots, including user-configured enterprise proxies.
    # Certificate and hostname validation are always required.
    try:
        import truststore
        return truststore.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    except ImportError:
        return ssl.create_default_context()

def fetch(url, limit=1024*1024):
    # Pin the socket to the validated IP; TLS still checks the original hostname.
    # This closes the DNS validation/connection rebinding gap.
    for _ in range(4):
        public_url(url)
        u=urllib.parse.urlsplit(url);port=u.port or (443 if u.scheme=='https' else 80)
        address=public_addresses(u.hostname,port)[0]
        connection=http.client.HTTPSConnection(u.hostname,port,timeout=15) if u.scheme=='https' else http.client.HTTPConnection(u.hostname,port,timeout=15)
        raw=socket.create_connection((address,port),timeout=15)
        connection.sock=tls_context().wrap_socket(raw,server_hostname=u.hostname) if u.scheme=='https' else raw
        try:
            connection.request('GET',urllib.parse.urlunsplit(('', '',u.path or '/',u.query,'')),headers={'User-Agent':'DragonGPT/4.0','Host':u.netloc})
            response=connection.getresponse()
            if response.status in (301,302,303,307,308):
                url=urllib.parse.urljoin(url,response.getheader('Location',''));continue
            if not 200<=response.status<300:raise ValueError('HTTP '+str(response.status))
            data=response.read(limit+1)
            if len(data)>limit: raise ValueError('Download exceeds size limit')
            return data,response.headers.get_content_type(),url
        finally:connection.close()
    raise ValueError('Too many redirects')

def metadata(p):
    s=p.stat()
    return {'path':str(p),'size':s.st_size,'modified':s.st_mtime,'directory':p.is_dir()}

def authorized_subdirectories(root,parent,dirs):
    allowed=[]
    for name in dirs:
        if name in ('.git','node_modules','.venv','build','dist','__pycache__'):continue
        try:safe_path(root,str(Path(parent)/name));allowed.append(name)
        except (ValueError,OSError):continue
    return allowed

def digest(p):
    if p.stat().st_size>16*1024*1024: raise ValueError('Change limited to 16 MB files')
    return hashlib.sha256(p.read_bytes()).hexdigest()

def read_document(p):
    if p.stat().st_size>16*1024*1024: raise ValueError('Document exceeds 16 MB')
    suffix=p.suffix.lower()
    if suffix=='.pdf':
        from pypdf import PdfReader
        r=PdfReader(p)
        return '\n'.join((page.extract_text() or '')[:12000] for page in r.pages[:20])[:MAX_TEXT]
    if suffix in ('.docx','.xlsx'):
        with zipfile.ZipFile(p) as z:
            if sum(i.file_size for i in z.infolist())>64*1024*1024: raise ValueError('Expanded document too large')
        if suffix=='.docx':
            from docx import Document
            doc=Document(p)
            return '\n'.join([x.text for x in doc.paragraphs]+[' | '.join(c.text for c in row.cells) for t in doc.tables for row in t.rows])[:MAX_TEXT]
        import openpyxl
        book=openpyxl.load_workbook(p,read_only=True,data_only=True,keep_links=False)
        try:
            lines=[]
            for sheet in book.worksheets[:8]:
                lines.append(sheet.title)
                for row in sheet.iter_rows(max_row=300,max_col=30,values_only=True):
                    lines.append(' | '.join(str(v) if v is not None else '' for v in row))
                    if sum(map(len,lines))>MAX_TEXT: return '\n'.join(lines)[:MAX_TEXT]
            return '\n'.join(lines)[:MAX_TEXT]
        finally: book.close()
    raise ValueError('Use PDF/DOCX/XLSX; legacy DOC/XLS need explicit conversion')

class ToolHost:
    def __init__(self):
        self.browser=None;self.playwright=None;self.context=None;self.page=None

    def execute(self, request):
        name=request['tool'];args=request['args'];root=request.get('root','');mode=request.get('mode',0)
        if type(mode) is not int or mode not in (1,2): raise PermissionError('Agent mode not authorized')
        if not isinstance(args,dict): raise ValueError('Object arguments required')
        if name in RISKY and request.get('approved') is not True: raise PermissionError('Exact operation approval required')
        if name in DEVELOPER and mode!=2: raise PermissionError('Developer mode required')
        if name.startswith('file_') or name in ('code_inspect','dev_run'):
            safe_path(root,'.',allow_root=True)
        if name=='file_list':
            folder=safe_path(root,args['path'],allow_root=True);rows=[];scanned=0;start=time.monotonic()
            for parent,dirs,files in os.walk(folder,followlinks=False):
                dirs[:]=authorized_subdirectories(root,parent,dirs)
                if not args.get('recursive',False): dirs[:]=[]
                for item in dirs+files:
                    scanned+=1
                    if scanned>5000 or time.monotonic()-start>5: return {'entries':rows,'truncated':True,'scanned':scanned}
                    try:
                        p=safe_path(root,str(Path(parent)/item))
                        if args.get('extension') and p.suffix.lower()!=args['extension'].lower(): continue
                        if args.get('modified_after',0)>p.stat().st_mtime: continue
                        rows.append(metadata(p))
                    except (OSError,ValueError): continue
                    if len(rows)>=200: return {'entries':rows,'truncated':True}
            return {'entries':rows,'truncated':False}
        if name in ('file_read','file_stat','file_document'):
            p=safe_path(root,args['path'])
            if name=='file_stat': return metadata(p)
            if name=='file_document': return {'text':bounded_text(read_document(p)),'source':str(p),'untrusted':True,'partial':True}
            offset=args.get('offset',0);limit=args.get('limit',12000)
            if type(offset) is not int or type(limit) is not int or not 0<=offset<=16*1024*1024 or not 1<=limit<=24000: raise ValueError('Invalid byte range')
            if p.suffix.lower() not in TEXT_EXT and p.name not in ('CMakeLists.txt','Makefile','LICENSE'): raise ValueError('Unsupported text type')
            with p.open('rb') as f: f.seek(offset);data=f.read(limit)
            return {'text':bounded_text(data.decode('utf-8-sig',errors='replace')),'offset':offset,'next_offset':offset+len(data),'eof':offset+len(data)>=p.stat().st_size,'source':str(p),'untrusted':True}
        if name=='file_search':
            folder=safe_path(root,args['path'],allow_root=True);query=args['query'];rows=[];start=time.monotonic();visited=0
            if not query or len(query)>200: raise ValueError('Invalid query')
            for parent,dirs,files in os.walk(folder,followlinks=False):
                dirs[:]=authorized_subdirectories(root,parent,dirs)
                for file in files:
                    visited+=1
                    if visited>5000 or time.monotonic()-start>5: return {'matches':rows,'truncated':True}
                    try:
                        p=safe_path(root,str(Path(parent)/file))
                        if query.casefold() in file.casefold(): rows.append({'path':str(p),'kind':'filename'})
                        if args.get('content',False) and p.suffix.lower() in TEXT_EXT and p.stat().st_size<1024*1024:
                            for i,line in enumerate(p.read_text(encoding='utf-8',errors='replace').splitlines()):
                                if query.casefold() in line.casefold(): rows.append({'path':str(p),'line':i+1,'text':bounded_text(line)[:300]})
                                if len(rows)>=80: return {'matches':rows[:80],'truncated':True,'untrusted':True}
                    except (OSError,ValueError): continue
            return {'matches':rows[:80],'truncated':len(rows)>80,'untrusted':True}
        if name in ('file_write','file_replace'):
            p=safe_path(root,args['path']);exists=p.exists();expected=args.get('sha256','')
            if exists and (not expected or digest(p)!=expected): raise ValueError('Preview hash missing or file changed; inspect again')
            if not exists and expected: raise ValueError('Expected existing file missing')
            if name=='file_replace':
                old=args['old'];new=args['new'];text=p.read_text(encoding='utf-8')
                if not old or old not in text: raise ValueError('Exact search text not found')
                text=text.replace(old,new)
            else: text=args['text']
            if len(text.encode('utf-8'))>1024*1024: raise ValueError('Write exceeds 1 MB')
            backup=None
            if exists:
                backup=p.with_name(p.name+'.dragon-backup-'+str(time.time_ns()));shutil.copy2(p,backup)
            # Same-directory exclusive temporary file + atomic replace.
            temp=p.with_name('.dragon-write-'+str(time.time_ns()))
            try:
                with temp.open('x',encoding='utf-8',newline='') as f: f.write(text);f.flush();os.fsync(f.fileno())
                if exists and digest(p)!=expected: raise ValueError('File changed during write')
                if not exists and p.exists(): raise ValueError('Destination appeared')
                if exists: os.replace(temp,p)
                else: os.rename(temp,p)
            finally: temp.unlink(missing_ok=True)
            return {'written':True,'sha256':digest(p),'backup':str(backup) if backup else None}
        if name=='file_hash':
            p=safe_path(root,args['path']);return {'sha256':digest(p),'path':str(p)}
        if name=='file_mkdir':
            p=safe_path(root,args['path']);p.mkdir(exist_ok=False);return {'created':p.is_dir(),'path':str(p)}
        if name in ('file_copy','file_move'):
            src=safe_path(root,args['source']);dst=safe_path(root,args['destination'])
            if not src.is_file() or dst.exists(): raise ValueError('Source must be a file and destination absent')
            before=digest(src)
            if name=='file_copy':
                with src.open('rb') as f,dst.open('xb') as out: shutil.copyfileobj(f,out)
            else: os.rename(src,dst)
            if digest(dst)!=before: raise ValueError('Result hash mismatch')
            return {'verified':True,'sha256':before,'source':str(src),'destination':str(dst),'undo':{'source':str(dst),'destination':str(src)} if name=='file_move' else None}
        if name=='file_trash':
            p=safe_path(root,args['path'])
            if not p.is_file() or digest(p)!=args['sha256']: raise ValueError('Delete requires current file hash')
            if os.name!='nt': raise ValueError('Windows recycle bin required')
            class Op(ctypes.Structure):
                _fields_=[('hwnd',ctypes.c_void_p),('func',ctypes.c_uint),('src',ctypes.c_wchar_p),('dst',ctypes.c_wchar_p),('flags',ctypes.c_ushort),('aborted',ctypes.c_int),('mapping',ctypes.c_void_p),('title',ctypes.c_wchar_p)]
            operation=Op(None,3,str(p)+'\0\0',None,0x40|0x10|0x400|0x4000,0,None,None)
            result=ctypes.windll.shell32.SHFileOperationW(ctypes.byref(operation))
            if result or operation.aborted or p.exists(): raise ValueError('Recycle operation failed or cancelled')
            return {'recycled':True,'restore':'Use Windows Recycle Bin'}
        if name=='code_inspect':
            folder=safe_path(root,args['path'],allow_root=True)
            return {'configuration':[x.name for x in folder.iterdir() if x.name in ('CMakeLists.txt','pyproject.toml','package.json','Cargo.toml','Makefile','requirements.txt')],
                    'files':self.execute({**request,'tool':'file_list','args':{'path':str(folder),'recursive':True}})}
        if name=='dev_run':
            folder=safe_path(root,args['path'],allow_root=True);action=args['action']
            commands={'configure':['cmake','-S','.','-B','build-agent','-G','Ninja'],
                      'build':['cmake','--build','build-agent','--parallel','2'],
                      'test':['ctest','--test-dir','build-agent','--output-on-failure','--timeout','30'],
                      'git_diff':['git','--no-pager','diff','--no-ext-diff','--no-textconv'],
                      'git_status':['git','status','--short']}
            if action not in commands: raise ValueError('Unknown development preset')
            started=time.monotonic()
            # No inherited API keys/proxy variables, shell expansion or arbitrary argv.
            env={k:v for k,v in os.environ.items() if not re.search('key|token|secret|password|proxy',k,re.I)}
            import tempfile
            with tempfile.TemporaryFile() as out:
                proc=subprocess.Popen(commands[action],cwd=folder,env=env,stdout=out,stderr=out,stdin=subprocess.DEVNULL,shell=False,creationflags=0x08000000 if os.name=='nt' else 0)
                try: code=proc.wait(timeout=90)
                except subprocess.TimeoutExpired: proc.kill();proc.wait();raise TimeoutError('Development command timed out')
                out.seek(0);text=out.read(24001).decode('utf-8',errors='replace')
            return {'exit_code':code,'output':bounded_text(text),'elapsed_ms':round((time.monotonic()-started)*1000),'success':code==0,'truncated':len(text)>24000}
        if name in ('web_read','web_search','github_release','web_download'):
            url=args.get('url','')
            if name=='web_search': url='https://www.bing.com/search?q='+urllib.parse.quote(args['query'])
            if name=='github_release':
                repo=args['repository']
                if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+',repo): raise ValueError('Invalid owner/repository')
                url='https://api.github.com/repos/'+repo+'/releases/latest'
            data,kind,url=fetch(url,16*1024*1024 if name=='web_download' else 1024*1024)
            if name=='web_download':
                p=safe_path(root,args['path'])
                if p.suffix.lower() in ('.exe','.dll','.msi','.ps1','.bat','.cmd','.vbs','.lnk','.scr'): raise ValueError('Executable downloads denied')
                with p.open('xb') as f:f.write(data)
                return {'saved':True,'path':str(p),'sha256':hashlib.sha256(data).hexdigest(),'source':url}
            text=data.decode('utf-8',errors='replace')
            if kind=='text/html':
                text=re.sub(r'<(script|style|noscript)\b[^>]*>.*?</\1>','',text,flags=re.S|re.I)
                text=html.unescape(re.sub('<[^>]+>',' ',text));text=re.sub(r'\s+',' ',text)
            return {'text':bounded_text(text),'source':url,'fetched_at':time.time(),'untrusted':True,'partial':len(text)>MAX_TEXT}
        if name.startswith('browser_'): return self.browser_tool(name,args,root)
        if name.startswith('desktop_'):
            script=Path(__file__).with_name('desktop.ps1')
            payload=json.dumps({'tool':name,'args':args})
            proc=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(script)],input=payload.encode('utf-8'),capture_output=True,timeout=15,creationflags=0x08000000)
            if proc.returncode:
                try:error=proc.stderr.decode('utf-8')
                except UnicodeDecodeError:error=proc.stderr.decode('mbcs',errors='replace')
                raise ValueError('UI Automation failed: '+error[:800])
            output=proc.stdout.decode('utf-8-sig');bounded_text(output)
            return json.loads(output)
        raise ValueError('Tool not registered')

    def browser_tool(self,name,args,root):
        if name=='browser_open':
            public_url(args['url'])
            if not self.browser:
                from playwright.sync_api import sync_playwright
                self.playwright=sync_playwright().start()
                self.browser=self.playwright.chromium.launch(channel='msedge',headless=False,args=['--disable-extensions'])
                self.context=self.browser.new_context(accept_downloads=True)
                self.context.set_default_timeout(8000)
                # Isolated profile: no access to the user's saved logins or cookies.
                def route_guard(route):
                    try: public_url(route.request.url);route.continue_()
                    except Exception: route.abort()
                self.context.route('**/*',route_guard)
            self.page=self.context.new_page();self.page.goto(args['url'],wait_until='domcontentloaded',timeout=20000)
        if not self.page: raise ValueError('Open a browser tab first')
        if name=='browser_tab':
            index=args['index']
            if type(index) is not int or not 0<=index<len(self.context.pages): raise ValueError('Unknown tab')
            self.page=self.context.pages[index];self.page.bring_to_front()
        elif name in ('browser_click','browser_fill','browser_upload','browser_download'):
            role=args['role'];label=args['name']
            if role not in ('button','link','textbox','checkbox','combobox','menuitem','tab','radio','spinbutton','file'): raise ValueError('Unsupported role')
            locator=self.page.get_by_label(label,exact=True) if role=='file' else self.page.get_by_role(role,name=label,exact=True)
            if locator.count()!=1: raise ValueError('Locator must match exactly one accessible element')
            if locator.get_attribute('type')=='password' or re.search('密码|password|token|secret',label,re.I): raise ValueError('Sensitive field denied')
            if name=='browser_click': locator.click()
            elif name=='browser_fill':
                if SECRET.search(args['text']): raise ValueError('Sensitive input denied')
                locator.fill(args['text'])
            elif name=='browser_upload':
                if locator.get_attribute('type')!='file':raise ValueError('Choose an actual labelled file input')
                locator.set_input_files(str(safe_path(root,args['path'])))
            else:
                p=safe_path(root,args['path'])
                if p.exists(): raise ValueError('Download target exists')
                if p.suffix.lower() in ('.exe','.dll','.msi','.ps1','.bat','.cmd','.lnk'): raise ValueError('Executable downloads denied')
                with self.page.expect_download(timeout=15000) as info: locator.click()
                download=info.value
                if download.failure(): raise ValueError('Download failed')
                if Path(download.path()).stat().st_size>16*1024*1024:raise ValueError('Browser download exceeds 16 MB')
                download.save_as(p)
                return {'saved':p.exists(),'path':str(p)}
        elif name=='browser_scroll': self.page.mouse.wheel(0,max(-1000,min(1000,args['delta'])))
        elif name not in ('browser_open','browser_snapshot','browser_tab'): raise ValueError('Unknown browser tool')
        # Form values, passwords, cookies and tokens are deliberately never returned.
        elements=self.page.locator('button,a,input,select,[role]').evaluate_all("els=>els.slice(0,100).filter(e=>e.type!=='password'&&!/password|token|secret|密码/i.test(e.getAttribute('aria-label')||e.name||'')).map(e=>({role:e.getAttribute('role')||({BUTTON:'button',A:'link',INPUT:'textbox',SELECT:'combobox'}[e.tagName]||''),name:(e.getAttribute('aria-label')||e.innerText||e.placeholder||'').slice(0,150)}))")
        text=self.page.locator('body').inner_text(timeout=5000)
        return {'url':self.page.url,'title':self.page.title(),'elements':elements,'text':bounded_text(text),
                'tabs':[{'index':i,'url':p.url} for i,p in enumerate(self.context.pages)],'untrusted':True}

def job_guard():
    """Worker exit/Qt kill closes the job and kills all child build/browser processes."""
    if os.name!='nt': return None
    from ctypes import wintypes
    class Basic(ctypes.Structure):
        _fields_=[('process_time',ctypes.c_int64),('job_time',ctypes.c_int64),('flags',wintypes.DWORD),('min_ws',ctypes.c_size_t),('max_ws',ctypes.c_size_t),('active',wintypes.DWORD),('affinity',ctypes.c_size_t),('priority',wintypes.DWORD),('scheduling',wintypes.DWORD)]
    class Io(ctypes.Structure): _fields_=[(x,ctypes.c_uint64) for x in ('r','w','o','rb','wb','ob')]
    class Extended(ctypes.Structure): _fields_=[('basic',Basic),('io',Io),('process_mem',ctypes.c_size_t),('job_mem',ctypes.c_size_t),('peak_proc',ctypes.c_size_t),('peak_job',ctypes.c_size_t)]
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateJobObjectW.restype=wintypes.HANDLE
    kernel.SetInformationJobObject.argtypes=[wintypes.HANDLE,ctypes.c_int,ctypes.c_void_p,wintypes.DWORD]
    kernel.AssignProcessToJobObject.argtypes=[wintypes.HANDLE,wintypes.HANDLE]
    kernel.GetCurrentProcess.restype=wintypes.HANDLE
    job=kernel.CreateJobObjectW(None,None);limits=Extended();limits.basic.flags=0x2000|0x200;limits.job_mem=2*1024**3
    if not job or not kernel.SetInformationJobObject(job,9,ctypes.byref(limits),ctypes.sizeof(limits)) or not kernel.AssignProcessToJobObject(job,kernel.GetCurrentProcess()):
        raise RuntimeError('Unable to establish process-tree cancellation/resource boundary')
    return job

def main():
    sys.stdin.reconfigure(encoding='utf-8');sys.stdout.reconfigure(encoding='utf-8')
    job=job_guard();host=ToolHost()
    for line in sys.stdin:
        request={}
        try:
            if len(line)>2*1024*1024: raise ValueError('Request too large')
            request=json.loads(line);start=time.monotonic();result=host.execute(request)
            response={'id':request['id'],'ok':True,'result':result,'elapsed_ms':round((time.monotonic()-start)*1000)}
        except Exception as e:
            response={'id':request.get('id'),'ok':False,'result':{'error':str(e)[:500],'kind':type(e).__name__}}
        print(json.dumps(response,ensure_ascii=False),flush=True)

if __name__=='__main__': main()

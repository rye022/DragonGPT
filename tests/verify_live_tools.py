"""Explicit integration probe against an isolated browser and a synthetic WinForms window.
No logged-in profiles, user documents or external mutations are used.
"""
import importlib.util
import json
from pathlib import Path
import subprocess
import time
BASE=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('host',BASE/'agent/worker.py');w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
ROOT=BASE/'work/live-tool-fixture';ROOT.mkdir(exist_ok=True)
host=w.ToolHost();results={}
def call(tool,args,approved=True):return host.execute({'tool':tool,'args':args,'root':str(ROOT),'mode':2,'approved':approved})
def main():
    form=None
    try:
        page=call('browser_open',{'url':'https://example.com'})
        results['browser_open']={'url':page['url'],'title':page['title']}
        name=next(e['name'] for e in page['elements'] if e['role']=='link')
        clicked=call('browser_click',{'role':'link','name':name});results['browser_click']={'url':clicked['url'],'title':clicked['title']}
        assert 'iana.org' in clicked['url']
        # Deterministic DOM fixture on the isolated tab, containing no remote form.
        host.page=host.context.new_page() # avoid inheriting the external site's CSP
        host.page.set_content('''<h1>DragonGPT local browser fixture</h1><label>Upload sample<input aria-label="Upload sample" type="file"></label><input aria-label="Task name"><button onclick="document.querySelector('h1').textContent='Submitted fixture'">Apply fixture</button><a role="button" href="https://example.com/dragon-download-fixture" download>Download fixture</a><input aria-label="Password" type="password" value="must-never-leak">''')
        call('browser_fill',{'role':'textbox','name':'Task name','text':'real fixture input'})
        assert host.page.get_by_role('textbox',name='Task name').input_value()=='real fixture input'
        (ROOT/'upload.txt').write_text('actual upload bytes',encoding='utf-8')
        call('browser_upload',{'role':'file','name':'Upload sample','path':'upload.txt'})
        assert host.page.locator('input[type=file]').evaluate('(e)=>e.files[0].name')=='upload.txt'
        host.context.route('https://example.com/dragon-download-fixture',lambda route:route.fulfill(status=200,headers={'Content-Type':'text/plain','Content-Disposition':'attachment; filename=sample.txt'},body='actual download bytes'))
        (ROOT/'download.txt').unlink(missing_ok=True)
        call('browser_download',{'role':'button','name':'Download fixture','path':'download.txt'})
        assert (ROOT/'download.txt').read_text()=='actual download bytes'
        try:call('browser_fill',{'role':'textbox','name':'Password','text':'denied'})
        except ValueError:results['browser_password_denied']=True
        else:raise AssertionError('Password must be denied')
        r=call('browser_click',{'role':'button','name':'Apply fixture'});assert 'Submitted fixture' in r['text'];assert 'must-never-leak' not in json.dumps(r)
        results['browser_form_upload_download']='passed (local deterministic DOM fixture)'
        host.page.screenshot(path=str(ROOT/'browser-fixture.png'))
        host.browser.close();host.playwright.stop();host.browser=None
        output=ROOT/'desktop-result.txt';output.unlink(missing_ok=True)
        form=subprocess.Popen(['powershell.exe','-NoProfile','-STA','-ExecutionPolicy','Bypass','-File',str(BASE/'tests/desktop_fixture.ps1'),'-ResultPath',str(output)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=0x08000000)
        fixture_ids=json.loads(form.stdout.readline().decode());fixture_handle=str(fixture_ids['window'])
        time.sleep(.5)
        focus=call('desktop_focus',{'handle':fixture_handle})
        assert focus['focused'], 'Windows denied activating the known synthetic test window'
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            try:
                r=call('desktop_inspect',{'handle':fixture_handle})
                if r.get('title')=='DragonGPT automation fixture':break
            except Exception as e:
                last_error=str(e)
            time.sleep(.3)
        else:raise AssertionError('Synthetic window inspection failed: '+locals().get('last_error','Unexpected window title'))
        if 'fixture-must-never-be-returned' in json.dumps(r):print('SYNTHETIC PASSWORD LEAK ROWS',json.dumps([x for x in r['controls'] if 'fixture-must-never' in str(x) or 'Password' in str(x)],ensure_ascii=False),flush=True)
        assert 'fixture-must-never-be-returned' not in json.dumps(r)
        controls=r['controls'];handle=str(r['handle']);input_id=next(x['automation_id'] for x in controls if x['automation_id']==str(fixture_ids['input']) or x['name']=='Fixture input');apply_id=next(x['automation_id'] for x in controls if x['automation_id']==str(fixture_ids['apply']) or x['name']=='Apply fixture')
        call('desktop_fill',{'handle':handle,'focus':True,'automation_id':input_id,'text':'actual UI Automation input'})
        call('desktop_invoke',{'handle':handle,'focus':True,'automation_id':apply_id})
        assert output.read_text(encoding='utf-8')=='actual UI Automation input'
        call('desktop_fill',{'handle':handle,'focus':True,'automation_id':input_id,'text':'actual UIA bounded mouse click'})
        call('desktop_click',{'handle':handle,'focus':True,'automation_id':apply_id})
        assert output.read_text(encoding='utf-8')=='actual UIA bounded mouse click'
        call('desktop_fill',{'handle':handle,'focus':True,'automation_id':input_id,'text':'actual Enter key result'})
        call('desktop_key',{'handle':handle,'focus':True,'key':'enter'})
        assert output.read_text(encoding='utf-8')=='actual Enter key result'
        results['desktop_key']='Enter invoked the fixture default button and verified disk result'
        try:call('desktop_fill',{'handle':handle,'focus':True,'automation_id':str(fixture_ids['password']),'text':'denied'})
        except ValueError:results['desktop_password_denied']=True
        else:raise AssertionError('Password must be denied')
        results['desktop_fill_invoke_mouse']='passed against synthetic WinForms controls'
    finally:
        if host.browser:host.browser.close()
        if form:form.terminate();form.wait(timeout=5)
        (BASE/'work/live-tools.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(results,ensure_ascii=False))
if __name__=='__main__':main()

"""Explicit synthetic CPU ASR + QWidget::grab contention measurement (not live FPS)."""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import statistics
import subprocess
import sys
import time
BASE=Path(__file__).resolve().parents[1]
child_code=r'''
import sys,time,wave,json,importlib.util
from pathlib import Path
root=Path(sys.argv[1]);spec=importlib.util.spec_from_file_location('asrworker',root/'asr/worker.py');w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
engine=w.RecognitionEngine({'engine':'local','model_path':str(root/'work/asr-model-base'),'device':'cpu','language':'zh','hotwords':'Python, C++'})
with wave.open(str(root/'work/asr-corpus/sample-3.wav'),'rb') as f:pcm=f.readframes(f.getnframes())
print('ready',flush=True);start=time.monotonic();latencies=[]
while time.monotonic()-start<22:latencies.append(engine.transcribe(pcm)['latency_ms'])
import ctypes
from ctypes import wintypes
class Memory(ctypes.Structure):
 _fields_=[('cb',wintypes.DWORD),('faults',wintypes.DWORD)]+[(x,ctypes.c_size_t) for x in ['peak_ws','ws','peak_pool','pool','peak_nonpaged','nonpaged','pagefile','peak_pagefile']]
kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=wintypes.HANDLE;handle=kernel.GetCurrentProcess()
psapi=ctypes.WinDLL('psapi');psapi.GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.POINTER(Memory),wintypes.DWORD]
mem=Memory();mem.cb=ctypes.sizeof(mem);rss=mem.peak_ws/1024**2 if psapi.GetProcessMemoryInfo(handle,ctypes.byref(mem),mem.cb) else None
kernel.GetProcessTimes.argtypes=[wintypes.HANDLE]+[ctypes.POINTER(wintypes.FILETIME)]*4
a,b,c,d=(wintypes.FILETIME() for _ in range(4));cpu=None
if kernel.GetProcessTimes(handle,ctypes.byref(a),ctypes.byref(b),ctypes.byref(c),ctypes.byref(d)):cpu=((c.dwHighDateTime<<32)+c.dwLowDateTime+(d.dwHighDateTime<<32)+d.dwLowDateTime)/1e7
print(json.dumps({'iterations':len(latencies),'median_inference_ms':sorted(latencies)[len(latencies)//2],'device':'cpu','threads':2,'peak_working_set_mb':rss,'cpu_seconds':cpu}),flush=True)
'''

def main():
    executable=BASE/'work/build/dragon_ui_perf_probe.exe'
    subprocess.run([str(executable),str(BASE/'work/performance-idle.json')],check=True,stdout=subprocess.DEVNULL,timeout=60)
    worker=subprocess.Popen([sys.executable,'-u','-c',child_code,str(BASE)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
    assert worker.stdout.readline().strip()=='ready'
    class Counters(ctypes.Structure):
        _fields_=[('cb',wintypes.DWORD),('faults',wintypes.DWORD)]+[(x,ctypes.c_size_t) for x in ['peak_ws','ws','peak_pool','pool','peak_nonpaged','nonpaged','pagefile','peak_pagefile']]
    psapi=ctypes.WinDLL('psapi');psapi.GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.POINTER(Counters),wintypes.DWORD]
    kernel=ctypes.WinDLL('kernel32');kernel.GetProcessTimes.argtypes=[wintypes.HANDLE]+[ctypes.POINTER(wintypes.FILETIME)]*4
    start=time.monotonic();peak=0
    with (BASE/'work/performance-asr.log').open('w') as out:
        render=subprocess.Popen([str(executable),str(BASE/'work/performance-with-asr.json')],stdout=out,stderr=out)
        while worker.poll() is None:
            counter=Counters();counter.cb=ctypes.sizeof(counter)
            if psapi.GetProcessMemoryInfo(wintypes.HANDLE(int(worker._handle)),ctypes.byref(counter),counter.cb):peak=max(peak,counter.peak_ws)
            time.sleep(.1)
        render.wait(timeout=30)
    rest=worker.stdout.read();stderr=worker.stderr.read();worker.wait(timeout=10)
    a,b,c,d=(wintypes.FILETIME() for _ in range(4));cpu=None
    if kernel.GetProcessTimes(wintypes.HANDLE(int(worker._handle)),ctypes.byref(a),ctypes.byref(b),ctypes.byref(c),ctypes.byref(d)):
        cpu=((c.dwHighDateTime<<32)+c.dwLowDateTime+(d.dwHighDateTime<<32)+d.dwLowDateTime)/1e7
    stress=json.loads(rest.strip().splitlines()[-1]);idle=json.loads((BASE/'work/performance-idle.json').read_text());busy=json.loads((BASE/'work/performance-with-asr.json').read_text())
    result={'idle_median_of_case_means_ms':statistics.median(x['mean_ms'] for x in idle['cases']),
            'asr_median_of_case_means_ms':statistics.median(x['mean_ms'] for x in busy['cases']),
            'asr_peak_working_set_mb':stress['peak_working_set_mb'],'asr_cpu_seconds':stress['cpu_seconds'],'elapsed_seconds':round(time.monotonic()-start,2),
            'asr':stress,'cases':18,'frames_per_case':30,'note':'Synthetic replay; sequential idle/stress runs, synchronous grab timing, not compositor FPS or real cloud TTFA.'}
    gpu=subprocess.run(['nvidia-smi','--query-gpu=name,memory.total,memory.used','--format=csv,noheader'],capture_output=True,text=True,timeout=10)
    result['gpu_after_cpu_stress']=gpu.stdout.strip();(BASE/'work/performance-summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(result,ensure_ascii=False))
if __name__=='__main__':main()

"""16kHz mono PCM16 + WebRTC VAD. Audio never saved unless benchmark explicitly does so.
Local inference uses an existing faster-whisper directory (no automatic downloads).
"""
from __future__ import annotations
import base64
from collections import deque
import io
import json
import math
import re
import struct
import sys
import time
import urllib.parse
import urllib.request
import uuid
import wave

class VadSegmenter:
    def __init__(self,sensitivity=2,silence_ms=1100,max_ms=30000):
        import webrtcvad
        self.vad=webrtcvad.Vad(max(0,min(3,int(sensitivity))))
        self.silence_frames=max(20,min(100,int(silence_ms/30)))
        self.max_frames=max_ms//30
        self.reset()

    def reset(self):
        self.carry=b'';self.pre=deque(maxlen=10);self.votes=deque(maxlen=5)
        self.frames=[];self.silence=0;self.speech=0;self.active=False

    def feed(self,pcm):
        self.carry+=pcm;results=[]
        while len(self.carry)>=960:
            frame=self.carry[:960];self.carry=self.carry[960:]
            voiced=self.vad.is_speech(frame,16000)
            samples=struct.unpack('<480h',frame)
            rms=math.sqrt(sum(v*v for v in samples)/480)/32768
            # WebRTC + level floor rejects digital silence; no second denoiser.
            voiced=voiced and rms>0.001
            if not self.active:
                self.pre.append(frame);self.votes.append(voiced)
                if len(self.votes)==5 and sum(self.votes)>=3:
                    self.active=True;self.frames=list(self.pre);self.speech=sum(self.votes);self.silence=0
            else:
                self.frames.append(frame);self.speech+=int(voiced);self.silence=0 if voiced else self.silence+1
                if self.silence>=self.silence_frames or len(self.frames)>=self.max_frames:
                    forced=len(self.frames)>=self.max_frames
                    if self.speech>=8:
                        results.append((b''.join(self.frames),forced))
                    remaining=self.carry;self.reset();self.carry=remaining
        return results

class RecognitionEngine:
    def __init__(self,config):
        self.config=config;self.model=None;self.device='cloud'
        if config['engine']=='local':
            from pathlib import Path
            from faster_whisper import WhisperModel
            path=Path(config['model_path'])
            if not path.is_dir() or not (path/'model.bin').exists(): raise ValueError('Select a prepared faster-whisper model directory; downloads are never implicit')
            self.device=config.get('device','cpu')
            if self.device not in ('cpu','cuda'): raise ValueError('Device must be cpu or cuda')
            try:
                self.model=WhisperModel(str(path),device=self.device,compute_type='int8' if self.device=='cpu' else 'int8_float16',cpu_threads=2,num_workers=1,local_files_only=True)
            except Exception:
                if self.device!='cuda': raise
                self.device='cpu (GPU initialization failed)'
                self.model=WhisperModel(str(path),device='cpu',compute_type='int8',cpu_threads=2,num_workers=1,local_files_only=True)

    def transcribe(self,pcm):
        cfg=self.config;start=time.monotonic();signals=[]
        samples=struct.unpack('<'+'h'*(len(pcm)//2),pcm)
        if not samples or math.sqrt(sum(x*x for x in samples)/len(samples))/32768<0.001:
            return {'text':'','signals':['输入音量过低或静音，未生成转写'],'device':self.device,'latency_ms':0}
        if cfg['engine']=='local':
            import numpy as np
            audio=np.frombuffer(pcm,dtype='<i2').astype('float32')/32768
            options={'language':cfg.get('language') or None,'beam_size':5,'vad_filter':False,
                     'initial_prompt':cfg.get('hotwords','')[:1000],'condition_on_previous_text':False}
            try: segments,info=self.model.transcribe(audio,**options);segments=list(segments)
            except Exception as gpu_error:
                if not self.device.startswith('cuda'): raise
                from faster_whisper import WhisperModel
                self.device='cpu (GPU inference failed)';self.model=WhisperModel(cfg['model_path'],device='cpu',compute_type='int8',cpu_threads=2,num_workers=1,local_files_only=True)
                segments,info=self.model.transcribe(audio,**options);segments=list(segments)
                signals.append('GPU推理失败，已回退CPU：'+type(gpu_error).__name__+': '+str(gpu_error)[:200])
            text=''.join(s.text for s in segments).strip()
            # These are reliability signals, never invented calibrated confidence.
            if any(s.avg_logprob < -1 or s.no_speech_prob>0.6 for s in segments): signals.append('模型低对数概率或高无语音概率')
            if re.search(r'(.{1,15})\1{6,}',text):
                text='';signals.append('重复转写异常，已丢弃文本')
        else:
            url=cfg['endpoint'];u=urllib.parse.urlsplit(url)
            if u.scheme!='https' or not u.hostname or u.username or u.password or u.fragment: raise ValueError('Cloud ASR requires an explicitly configured HTTPS endpoint')
            wav=io.BytesIO()
            with wave.open(wav,'wb') as f:f.setnchannels(1);f.setsampwidth(2);f.setframerate(16000);f.writeframes(pcm)
            boundary='DragonGPT'+uuid.uuid4().hex;parts=[]
            def field(key,value):parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"\r\n\r\n{value}\r\n'.encode())
            field('model',cfg['cloud_model']);field('response_format','json')
            if cfg.get('language'):field('language',cfg['language'])
            if cfg.get('hotwords'):field('prompt',cfg['hotwords'][:1000])
            parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="speech.wav"\r\nContent-Type: audio/wav\r\n\r\n'.encode()+wav.getvalue()+f'\r\n--{boundary}--\r\n'.encode())
            class NoRedirect(urllib.request.HTTPRedirectHandler):
                def redirect_request(self,*args,**kwargs):return None
            request=urllib.request.Request(url,data=b''.join(parts),headers={'Authorization':'Bearer '+cfg['key'],'Content-Type':'multipart/form-data; boundary='+boundary})
            opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),NoRedirect())
            with opener.open(request,timeout=30) as response:
                data=response.read(65537)
                if len(data)>65536:raise ValueError('Cloud ASR response too large')
                text=json.loads(data)['text']
            signals.append('云端未提供可校准置信度')
        return {'text':text[:4000],'signals':signals,'device':self.device,'latency_ms':round((time.monotonic()-start)*1000)}

def main():
    sys.stdin.reconfigure(encoding='utf-8');sys.stdout.reconfigure(encoding='utf-8')
    # Heavy model imports have occasionally printed progress to stdout. Keep wire clean.
    wire=sys.stdout;sys.stdout=sys.stderr
    def emit(data):print(json.dumps(data,ensure_ascii=False),file=wire,flush=True)
    config=json.loads(sys.stdin.readline());vad=VadSegmenter(config.get('sensitivity',2),config.get('silence_ms',1100));engine=RecognitionEngine(config)
    emit({'event':'ready','device':engine.device})
    for line in sys.stdin:
        message=json.loads(line)
        if message.get('finish') is True:
            if not vad.active or vad.speech<8:
                emit({'event':'error','error':'未检测到足够语音，没有生成指令'});return
            ready=[(b''.join(vad.frames)+vad.carry,False)]
        else:
            pcm=base64.b64decode(message['pcm'],validate=True)
            if len(pcm)>32000:raise ValueError('PCM frame too large')
            ready=vad.feed(pcm)
        for segment,forced in ready:
            emit({'event':'processing','duration_ms':len(segment)//32})
            try:
                result=engine.transcribe(segment)
                if forced:result['signals'].append('达到单段时长上限，请检查是否截断')
                emit({'event':'final',**result})
            except Exception as e:emit({'event':'error','error':type(e).__name__+': '+str(e)[:300]})
            return # one stable final result per capture epoch, no duplicate submission

if __name__=='__main__':
    try:main()
    except Exception as e:
        print(json.dumps({'event':'error','error':type(e).__name__+': '+str(e)[:300]},ensure_ascii=False),file=sys.__stdout__,flush=True)

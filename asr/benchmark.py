"""Evaluate labelled WAV manifests. Never infer accuracy without references.
Usage: python asr/benchmark.py --manifest corpus.json --model <prepared-dir> --device cpu
Manifest: [{"audio":"relative.wav","reference":"人工核对文本","category":"中文", "baseline":"可选原引擎输出"}]
Synthetic audio must be marked synthetic=true; results cannot establish real-mic accuracy.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import time
import wave

def distance(a,b):
    row=list(range(len(b)+1))
    for i,x in enumerate(a,1):
        new=[i]
        for j,y in enumerate(b,1):new.append(min(new[-1]+1,row[j]+1,row[j-1]+(x!=y)))
        row=new
    return row[-1]

def normalized(text):return re.sub(r'[\s，。！？、,.!?；;：:「」“”\"\']','',text).casefold()

def evaluate(rows):
    errors=characters=word_errors=words=0
    for r in rows:
        a=normalized(r['reference']);b=normalized(r['hypothesis']);e=distance(a,b)
        r['char_errors']=e;r['reference_chars']=len(a);r['cer']=e/len(a) if a else None
        errors+=e;characters+=len(a)
        wa=r['reference'].casefold().split();wb=r['hypothesis'].casefold().split()
        word_errors+=distance(wa,wb);words+=len(wa)
    return {'cer':errors/characters if characters else None,'wer_whitespace':word_errors/words if words else None,
            'note':'WER uses whitespace tokens; Chinese CER is the meaningful measure. Intent/start loss/truncation need manual labels.'}

def main():
    p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True);p.add_argument('--model',required=True);p.add_argument('--device',choices=['cpu','cuda'],default='cpu');p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    import importlib.util
    spec=importlib.util.spec_from_file_location('asr_worker',Path(__file__).with_name('worker.py'));module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    start=time.monotonic();engine=module.RecognitionEngine({'engine':'local','model_path':args.model,'device':args.device,'language':'zh','hotwords':'C++, Python, GitHub, Codex, DeepSeek, DragonGPT, Live2D, API, Agent, RAII, STL'})
    load_ms=round((time.monotonic()-start)*1000);manifest=json.loads(args.manifest.read_text(encoding='utf-8'));rows=[]
    for item in manifest:
        if not isinstance(item.get('reference'),str):raise ValueError('Reference transcript required for every recording')
        audio=args.manifest.parent/item['audio']
        with wave.open(str(audio),'rb') as f:
            if (f.getnchannels(),f.getsampwidth(),f.getframerate())!=(1,2,16000):raise ValueError('Benchmark requires 16kHz mono PCM16')
            pcm=f.readframes(f.getnframes())
        result=engine.transcribe(pcm);rows.append({**item,'hypothesis':result['text'],'latency_ms':result['latency_ms'],'device':result['device'],'signals':result['signals']})
    report={'model':args.model,'requested_device':args.device,'load_ms':load_ms,'samples':rows,'metrics':evaluate(rows),
            'synthetic_only':all(r.get('synthetic',False) for r in rows),'live_microphone_accuracy_verified':False,
            'intent_accuracy':None,'sentence_start_loss_rate':None,'truncation_rate':None,'tts_echo_false_triggers':None,
            'limits':'CER/WER describe only these labelled samples. No measured human speech improvement or full-duplex AEC claim.'}
    baseline=[{**r,'hypothesis':r['baseline']} for r in rows if isinstance(r.get('baseline'),str)]
    if baseline:report['baseline_metrics']=evaluate(baseline)
    args.output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in report.items() if k!='samples'},ensure_ascii=False))

if __name__=='__main__':main()

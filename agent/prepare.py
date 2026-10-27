"""Explicit, user-invoked local runtime installation. Never called by a model tool."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import venv

def main():
    p=argparse.ArgumentParser();p.add_argument('--with-asr',action='store_true');p.add_argument('--model',choices=['base','small']);args=p.parse_args()
    root=Path(__file__).resolve().parents[1];runtime=root/'runtime'/'agent'
    venv.EnvBuilder(with_pip=True).create(runtime)
    python=runtime/('Scripts/python.exe' if sys.platform=='win32' else 'bin/python')
    requirements=[root/'agent/requirements-tested.txt']
    if args.with_asr:requirements.append(root/'asr/requirements-tested.txt')
    for file in requirements:subprocess.run([str(python),'-m','pip','install','-r',str(file)],check=True)
    ready={'python':str(python),'model_path':''}
    if args.model:
        if not args.with_asr:p.error('--model requires --with-asr')
        destination=root/'runtime'/'asr-model'
        script='from huggingface_hub import snapshot_download; import sys; snapshot_download(sys.argv[1],local_dir=sys.argv[2],allow_patterns=["model.bin","config.json","tokenizer.json","vocabulary.*","preprocessor_config.json"])'
        subprocess.run([str(python),'-c',script,'Systran/faster-whisper-'+args.model,str(destination)],check=True)
        ready['model_path']=str(destination)
    (root/'agent/ready.json').write_text(json.dumps(ready,indent=2),encoding='utf-8')
    if args.with_asr:(root/'asr/ready.json').write_text(json.dumps(ready,indent=2),encoding='utf-8')
    print('Prepared runtime:',python)
    if not args.model:print('No ASR weights downloaded. Select an existing model directory in Speech settings.')

if __name__=='__main__':main()

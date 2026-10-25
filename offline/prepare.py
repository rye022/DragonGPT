"""Download the official pinned checkpoint and verify offline speech synthesis."""
import argparse
import base64
import importlib.metadata
import json
import os
from pathlib import Path
import subprocess
import sys
import wave

REPOSITORY = "Qwen/Qwen3-TTS-12Hz-0.6B-CustomVoice"
REVISION = "85e237c12c027371202489a0ec509ded67b5e4b5"

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime", required=True)
    parser.add_argument("--skip-download", action="store_true")
    args = parser.parse_args()
    runtime = Path(args.runtime).resolve()
    model = runtime / "model"
    if not args.skip_download:
        from huggingface_hub import snapshot_download
        snapshot_download(REPOSITORY, revision=REVISION, local_dir=model)
    print("Verifying inference with all network connections blocked...", flush=True)
    environment = os.environ.copy()
    environment.update(HF_HUB_OFFLINE="1", TRANSFORMERS_OFFLINE="1", HF_HUB_DISABLE_TELEMETRY="1", PYTHONUTF8="1")
    for key in ["DEEPSEEK_API_KEY", "OPENAI_API_KEY", "DASHSCOPE_API_KEY"]:
        environment.pop(key, None)
    command = {"id": 1, "text": "你好，我是霜翎。现在没有网络，我也能用离线苏瑶的声音陪你说话。"}
    result = subprocess.run(
        [sys.executable, "-u", str(Path(__file__).with_name("worker.py")), "--model", str(model)],
        input=json.dumps(command, ensure_ascii=False)+"\n", capture_output=True,
        text=True, encoding="utf-8", errors="replace", timeout=300, env=environment,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    messages = [json.loads(line) for line in result.stdout.splitlines() if line.strip().startswith("{")]
    speech = next((item for item in messages if item.get("id") == 1 and "pcm" in item), None)
    if result.returncode or speech is None:
        (runtime / "setup-diagnostic.txt").write_text(result.stderr[-16000:]+result.stdout[-4000:], encoding="utf-8")
        raise RuntimeError("Offline inference failed; see offline-runtime/setup-diagnostic.txt")
    pcm = base64.b64decode(speech["pcm"], validate=True)
    if len(pcm) < 48000 or len(pcm) % 2 or not any(pcm):
        raise RuntimeError("Offline inference produced invalid/silent audio")
    with wave.open(str(runtime/"serena-offline-test.wav"), "wb") as output:
        output.setnchannels(1); output.setsampwidth(2); output.setframerate(24000); output.writeframes(pcm)
    versions = {name: importlib.metadata.version(name) for name in ["qwen-tts", "torch", "transformers"]}
    ready = {"model": REPOSITORY, "revision": REVISION, "versions": versions, "python": sys.executable,
             "network_blocked_test": True, "device": messages[0].get("device"), "test_pcm_bytes": len(pcm)}
    (runtime/"ready.json").write_text(json.dumps(ready, indent=2), encoding="utf-8")
    print("PASS: offline Serena synthesized new Chinese speech without networking.", flush=True)

if __name__ == "__main__":
    main()

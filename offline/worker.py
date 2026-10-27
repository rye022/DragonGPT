"""Local-only Qwen3-TTS worker. No API keys, network calls or voice cloning."""
import argparse
import base64
import contextlib
import json
import os
import socket
import sys

os.environ.update(HF_HUB_OFFLINE="1", TRANSFORMERS_OFFLINE="1", HF_HUB_DISABLE_TELEMETRY="1")

def deny_network(*args, **kwargs):
    raise RuntimeError("Networking is disabled in the offline speech worker")

socket.create_connection = deny_network
socket.socket.connect = deny_network

def emit(value):
    print(json.dumps(value, ensure_ascii=False), flush=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True)
    args = parser.parse_args()
    with contextlib.redirect_stdout(sys.stderr):
        import numpy as np
        import torch
        from qwen_tts import Qwen3TTSModel
        torch.set_num_threads(min(8, os.cpu_count() or 4))
        gpu = torch.cuda.is_available()
        model = Qwen3TTSModel.from_pretrained(
            args.model, device_map="cuda:0" if gpu else "cpu",
            dtype=torch.bfloat16 if gpu else torch.float32,
            attn_implementation="sdpa", local_files_only=True,
        )
    emit({"event": "loaded", "device": "cuda" if gpu else "cpu"})
    for line in sys.stdin:
        identifier = -1
        try:
            command = json.loads(line)
            identifier = int(command["id"])
            text = command["text"]
            if not isinstance(text, str) or not 1 <= len(text) <= 600:
                raise ValueError("Invalid text")
            with contextlib.redirect_stdout(sys.stderr), torch.inference_mode():
                waves, rate = model.generate_custom_voice(
                    text=text, speaker="Serena", language="Chinese",
                    do_sample=True, max_new_tokens=2400,
                )
            wave = np.asarray(waves[0], dtype=np.float32).reshape(-1)
            if rate != 24000:
                wave = np.interp(np.arange(int(len(wave) * 24000 / rate)) * rate / 24000,
                                 np.arange(len(wave)), wave)
            pcm = (np.clip(wave, -1, 1) * 32767).astype("<i2").tobytes()
            emit({"id": identifier, "pcm": base64.b64encode(pcm).decode("ascii")})
        except Exception as error:
            emit({"id": identifier, "error": type(error).__name__})

if __name__ == "__main__":
    main()

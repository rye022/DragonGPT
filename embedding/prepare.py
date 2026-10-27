"""Explicit setup only: download public model weights, never user data."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request
import sys
import sysconfig

REVISION = "9507db33464b5da99a532ac26b2a251767cbc62b"
REPO = "onnx-community/bge-small-zh-v1.5-ONNX"

def prepare(destination, python):
    destination = Path(destination).resolve()
    model = destination / "model"
    model.mkdir(parents=True, exist_ok=True)
    digests = {}
    for local, remote in {"model.onnx": "onnx/model_quantized.onnx", "model_quantized.onnx_data": "onnx/model_quantized.onnx_data", "tokenizer.json": "tokenizer.json", "README.md": "README.md"}.items():
        target = model / local
        if not target.exists():
            partial = target.with_suffix(target.suffix + ".part")
            with urllib.request.urlopen(f"https://huggingface.co/{REPO}/resolve/{REVISION}/{remote}", timeout=120) as response, partial.open("wb") as output:
                while chunk := response.read(1024*1024):
                    output.write(chunk)
            partial.replace(target)
        with target.open("rb") as source:
            digests[local] = hashlib.file_digest(source, "sha256").hexdigest()
        print(f"{local}: {target.stat().st_size} bytes", flush=True)
    (model / "manifest.json").write_text(json.dumps({"repo": REPO, "revision": REVISION, "license": "mit", "sha256": digests}, indent=2), encoding="utf-8")
    # Windows venv python.exe is a redirector which can leave its child alive when
    # a GUI controller kills only the launcher. Start the real interpreter and
    # provide this isolated environment's dependency directory explicitly.
    (destination / "ready.json").write_text(json.dumps({"format": 2, "python": str(Path(sys._base_executable).resolve()), "site_packages": sysconfig.get_path("purelib"), "model": "model"}, indent=2), encoding="utf-8")

if __name__ == "__main__":
    import sys
    parser = argparse.ArgumentParser()
    parser.add_argument("destination")
    parser.add_argument("--python", default=sys.executable)
    args = parser.parse_args()
    prepare(args.destination, args.python)

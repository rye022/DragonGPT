"""Build local source/runtime archives with an explicit privacy filter."""
from pathlib import Path
import hashlib
import json
import os
import zipfile

PUBLIC_DOCS = {
    "DRAGONGPT_4_ARCHITECTURE.md", "DRAGONGPT_4_MANUAL.md", "DRAGONGPT_4_TEST_REPORT.md", "RELEASE_v4.0.0.md",
    "USER_GUIDE_ZH.md", "V3_BASELINE.md", "V3_ARCHITECTURE.md", "V3_VALIDATION.md", "RELEASE_v4.0.0.md",
    "CLIPBOARD_AND_PERFORMANCE.md", "character-lock.md", "COMPANIONSHIP.md",
    "FOCUS_AND_WATER.md", "EMOTIONS.md", "LOCAL_BRAIN_AND_MEMORY.md", "connection.md", "voice.md",
    "DragonGPT_3.0_完整使用手册.md", "DragonGPT_3.0_最终审查报告.md", "DragonGPT_3.0_功能清单.md",
    "DragonGPT_3.0_模块依赖关系.md", "DragonGPT_3.0_测试结果.md",
}
BLOCKED_DIRECTORIES = {"data", "runtime", "offline-runtime", "voice-cache", "__pycache__", ".git", "build", "dist", "outputs", "logs", "backups", "model", "models", "venv", "work", ".venv"}

def include(relative):
    if any(p.lower() in BLOCKED_DIRECTORIES for p in relative.parts):
        return False
    name = relative.name.lower()
    if name.startswith(".env") or name in {"ready.json", ".installation.json"} or any(part in name for part in (".sqlite", ".dpapi")):
        return False
    if relative.suffix.lower() in {".pyc", ".log", ".lock", ".ini", ".bak", ".db", ".pem", ".pfx", ".key", ".p12", ".onnx", ".safetensors", ".pt", ".pth"}:
        return False
    if relative.parts[0] == "docs" and relative.name not in PUBLIC_DOCS and "screenshots" not in relative.parts and "audit" not in relative.parts:
        return False
    return True

def archive(root, destination):
    accepted = []
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for parent, dirs, files in os.walk(root, followlinks=False):
            dirs[:]=sorted(d for d in dirs if d.lower() not in BLOCKED_DIRECTORIES and not (Path(parent)/d).is_symlink())
            for filename in sorted(files):
                path=Path(parent)/filename
                relative = path.relative_to(root)
                if path.is_file() and not path.is_symlink() and include(relative):
                    name = "DragonGPT/" + relative.as_posix()
                    output.write(path, name)
                    accepted.append(name)
    with zipfile.ZipFile(destination) as result:
        if result.testzip() is not None:
            raise RuntimeError("Archive integrity failed")
        if any(not include(Path(name).relative_to("DragonGPT")) for name in result.namelist()):
            raise RuntimeError("Archive privacy check failed")
    return {"file": destination.name, "files": len(accepted), "bytes": destination.stat().st_size,
            "sha256": hashlib.file_digest(destination.open("rb"), "sha256").hexdigest(), "privacy_check": "passed"}

if __name__ == "__main__":
    project = Path(__file__).resolve().parent
    outputs = project / "outputs"
    outputs.mkdir(exist_ok=True)
    version = "4.0.0"
    records = [archive(project / "dist" / "DragonGPT", outputs / f"DragonGPT-v{version}-Windows-x64.zip"),
               archive(project, outputs / f"DragonGPT-v{version}-source.zip")]
    (outputs / "打包校验.json").write_text(json.dumps({"version": version, "archives": records,
        "exclusions": ["user databases", "DPAPI keys", "INI settings", "Python environments", "machine readiness paths", "caches", "personal historical audit documents"]}, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(records, ensure_ascii=False))

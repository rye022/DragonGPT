"""Build local source/runtime archives with an explicit privacy filter."""
from pathlib import Path
import hashlib
import json
import zipfile

PUBLIC_DOCS = {
    "LOCAL_BRAIN_AND_MEMORY.md", "UPGRADE_AUDIT.md", "character-lock.md",
    "COMPREHENSIVE_FIX.md", "COMPANIONSHIP.md", "EMOTIONS.md",
    "FOCUS_AND_WATER.md", "PERFORMANCE_AND_MEMORY.md", "connection.md", "voice.md",
    "validation.md",
    "CLIPBOARD_AND_PERFORMANCE.md",
}
BLOCKED_DIRECTORIES = {"data", "runtime", "offline-runtime", "voice-cache", "__pycache__", ".git", "build", "dist"}

def include(relative):
    if any(p.lower() in BLOCKED_DIRECTORIES for p in relative.parts):
        return False
    name = relative.name.lower()
    if name in {"ready.json", ".installation.json"} or any(part in name for part in (".sqlite", ".dpapi")):
        return False
    if relative.suffix.lower() in {".pyc", ".log", ".lock", ".ini", ".bak", ".db"}:
        return False
    if relative.parts[0] == "docs" and relative.name not in PUBLIC_DOCS:
        return False
    return True

def archive(root, destination):
    accepted = []
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for path in sorted(root.rglob("*")):
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
    outputs = project.parent
    version = "2.0.1"
    records = [archive(project / "dist" / "DragonGPT", outputs / f"DragonGPT-v{version}-Windows-x64.zip"),
               archive(project, outputs / f"DragonGPT-v{version}-source.zip")]
    (outputs / "打包校验.json").write_text(json.dumps({"version": version, "archives": records,
        "exclusions": ["user databases", "DPAPI keys", "INI settings", "Python environments", "machine readiness paths", "caches", "personal historical audit documents"]}, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(records, ensure_ascii=False))

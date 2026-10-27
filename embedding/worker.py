"""Offline ONNX sentence embeddings. JSONL stdin/stdout; never opens a network connection."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import sqlite3
import sys
import time

os.environ.update(HF_HUB_OFFLINE="1", TRANSFORMERS_OFFLINE="1", TOKENIZERS_PARALLELISM="false")
import numpy as np
import onnxruntime as ort
from tokenizers import Tokenizer
import socket

ort.disable_telemetry_events()
def no_network(*args, **kwargs):
    raise RuntimeError("Network is disabled in the local embedding worker")
socket.socket = no_network
socket.create_connection = no_network
socket.getaddrinfo = no_network

MODEL = "bge-small-zh-v1.5/9507db33/int8-cls"

def fingerprint(row):
    return hashlib.sha256((row["content"] + "\n" + row["tags"] + "\n" + row["layer"]).encode()).hexdigest()

def credential(text):
    return bool(re.search(r"sk-[A-Za-z0-9_-]{8,}|(?:api[ _-]?key|password|密码|密钥)\s*[:=：是]\s*[^\s，。]{4,}|Bearer\s+\S{8,}|BEGIN .*PRIVATE KEY", text, re.I))

class Engine:
    def __init__(self, model_dir, database):
        model_dir = Path(model_dir)
        manifest = json.loads((model_dir / "manifest.json").read_text(encoding="utf-8"))
        for name, digest in manifest["sha256"].items():
            if Path(name).name != name or hashlib.file_digest((model_dir / name).open("rb"), "sha256").hexdigest() != digest:
                raise ValueError("Embedding model checksum mismatch")
        options = ort.SessionOptions()
        options.intra_op_num_threads = 2
        options.inter_op_num_threads = 1
        options.log_severity_level = 3
        self.session = ort.InferenceSession(str(model_dir / "model.onnx"), sess_options=options, providers=["CPUExecutionProvider"])
        self.tokenizer = Tokenizer.from_file(str(model_dir / "tokenizer.json"))
        self.tokenizer.enable_truncation(max_length=128)
        self.tokenizer.enable_padding()
        self.db = sqlite3.connect(database, timeout=2)
        self.db.row_factory = sqlite3.Row
        self.db.execute("PRAGMA foreign_keys=ON")

    def encode(self, texts):
        encoded = self.tokenizer.encode_batch(texts)
        ids = np.array([v.ids for v in encoded], dtype=np.int64)
        mask = np.array([v.attention_mask for v in encoded], dtype=np.int64)
        inputs = {"input_ids": ids, "attention_mask": mask, "token_type_ids": np.zeros_like(ids)}
        output = self.session.run(None, {v.name: inputs[v.name] for v in self.session.get_inputs()})[0]
        if output.ndim == 3:
            output = output[:, 0, :]  # BGE uses CLS pooling, not mean pooling.
        output = output / np.maximum(np.linalg.norm(output, axis=1, keepdims=True), 1e-9)
        return output.astype("<f4")

    def index(self):
        # Limit each pass and batch inference. UI writes remain short and independent.
        rows = self.db.execute("""SELECT m.* FROM memories m LEFT JOIN memory_vectors v ON m.id=v.memory_id
            WHERE (v.memory_id IS NULL OR v.model<>?) AND (m.expires_at IS NULL OR m.expires_at='' OR m.expires_at>strftime('%Y-%m-%dT%H:%M:%fZ','now'))
            ORDER BY m.id DESC LIMIT 256""", (MODEL,)).fetchall()
        rows = [r for r in rows if not credential(r["content"])]
        for begin in range(0, len(rows), 8):
            batch = rows[begin:begin+8]
            vectors = self.encode([r["content"] for r in batch])
            with self.db:
                for row, vector in zip(batch, vectors):
                    # A delete/edit while inference runs must not resurrect an old vector.
                    current = self.db.execute("SELECT content,tags,layer FROM memories WHERE id=?", (row["id"],)).fetchone()
                    if current is None or fingerprint(current) != fingerprint(row):
                        continue
                    self.db.execute("INSERT OR REPLACE INTO memory_vectors VALUES(?,?,?,?,?,strftime('%Y-%m-%dT%H:%M:%fZ','now'))",
                                    (row["id"], MODEL, fingerprint(row), vector.tobytes(), len(vector)))
        return len(rows)

    def search(self, query, limit=3, layer="", source=""):
        if layer and layer not in ("semantic", "episodic", "relationship"):
            raise ValueError("Invalid memory layer")
        self.index()
        # v1.5 supports no instruction; local conversational regression performs
        # better with the original question than the passage-retrieval prefix.
        vector = self.encode([str(query)[:2000]])[0]
        rows = self.db.execute("""SELECT m.*,v.vector,v.fingerprint,v.dimensions FROM memory_vectors v JOIN memories m ON m.id=v.memory_id
            WHERE v.model=? AND (m.expires_at IS NULL OR m.expires_at='' OR m.expires_at>strftime('%Y-%m-%dT%H:%M:%fZ','now'))
            AND (?='' OR m.layer=?) AND (?='' OR m.source=?) LIMIT 20000""", (MODEL,layer,layer,source,source)).fetchall()
        ranked = []
        for row in rows:
            if credential(row["content"]):
                continue
            if row["fingerprint"] != fingerprint(row) or row["dimensions"] != len(vector):
                continue
            other = np.frombuffer(row["vector"], dtype="<f4")
            if len(other) != len(vector) or not np.isfinite(other).all():
                continue
            score = float(np.dot(vector, other))
            if score >= 0.48:
                ranked.append({"id": row["id"], "score": score, "fingerprint": row["fingerprint"]})
        return sorted(ranked, key=lambda r: r["score"], reverse=True)[:max(0,min(int(limit),20))]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True)
    parser.add_argument("--database", required=True)
    args = parser.parse_args()
    engine = Engine(args.model, args.database)
    for line in sys.stdin:
        request = {}
        try:
            if len(line) > 20000:
                raise ValueError("Request too large")
            request = json.loads(line)
            started = time.monotonic()
            if request.get("operation") == "index":
                response = {"indexed": engine.index()}
            else:
                response = {"matches": engine.search(request.get("query", ""), request.get("limit", 3), request.get("layer", ""), request.get("source", ""))}
            response.update(id=request.get("id", 0), model=MODEL, process_id=os.getpid(), elapsed_ms=round((time.monotonic()-started)*1000))
        except Exception as error:
            # Never echo query text, database content or a full exception message.
            response = {"id": request.get("id", 0), "error": type(error).__name__}
        print(json.dumps(response, ensure_ascii=False), flush=True)

if __name__ == "__main__":
    main()

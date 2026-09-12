#!/usr/bin/env python3
"""Download pinned public assets; no Python inference dependencies are needed."""
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    manifest = json.loads((ROOT / "models.lock.json").read_text())
    for item in manifest["files"]:
        path = ASSETS / item["path"]
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.is_file() and digest(path) == item["sha256"]:
            print(f"Verified cached {item['path']}", flush=True)
            continue
        print(f"Downloading {item['path']}", flush=True)
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False, suffix=".part") as stream:
            temporary = Path(stream.name)
        try:
            subprocess.run(["curl", "-L", "--http1.1", "--fail", "--retry", "3", "--retry-all-errors", "--connect-timeout", "20",
                            "--max-time", "600", item["url"], "-o", str(temporary)], check=True)
            if digest(temporary) != item["sha256"]:
                raise RuntimeError(f"SHA256 mismatch: {item['path']}")
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
    archive = ASSETS / "onnxruntime-linux-x64-1.30.0.tgz"
    with tarfile.open(archive) as bundle:
        bundle.extractall(ASSETS, filter="data")
    print("Ready: PP-OCRv6 small + ONNX Runtime CPU 1.30.0")


if __name__ == "__main__":
    main()

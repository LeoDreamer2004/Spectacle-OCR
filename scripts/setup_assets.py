#!/usr/bin/env python3
"""Download pinned public assets; no Python inference dependencies are needed."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(
        description="Download and verify pinned PP-OCR models and ONNX Runtime into assets/.",
        epilog="Requires Python 3.12+. Downloads require curl and access to Hugging Face/GitHub. "
               "Standard curl proxy environment variables are supported. Rerun after a failure; "
               "verified files are reused, incomplete files are downloaded again. "
               "Rust crates and system build dependencies are installed separately.",
    )
    parser.add_argument("--proxy", metavar="URL", help="curl proxy URL, e.g. http://127.0.0.1:7890")
    parser.add_argument("--offline", action="store_true", help="verify cached assets and extract the runtime without downloading")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "models.lock.json").read_text())
    missing = []
    for item in manifest["files"]:
        path = ASSETS / item["path"]
        if path.is_file() and digest(path) == item["sha256"]:
            print(f"Verified cached {item['path']}", flush=True)
            continue
        missing.append(item)
    if args.offline and missing:
        raise RuntimeError("Missing or invalid cached assets: " + ", ".join(item["path"] for item in missing))
    if missing and not shutil.which("curl"):
        raise RuntimeError("curl is required to download assets. Install curl and rerun this script.")
    for item in missing:
        path = ASSETS / item["path"]
        path.parent.mkdir(parents=True, exist_ok=True)
        print(f"Downloading {item['path']}", flush=True)
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False, suffix=".part") as stream:
            temporary = Path(stream.name)
        try:
            subprocess.run(["curl", *(["--proxy", args.proxy] if args.proxy else []),
                            "-L", "--http1.1", "--fail", "--retry", "3", "--retry-all-errors", "--connect-timeout", "20",
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
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError, tarfile.TarError) as error:
        print(f"Asset setup failed: {error}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nDownload interrupted. Rerun this script to retry.", file=sys.stderr)
        sys.exit(130)

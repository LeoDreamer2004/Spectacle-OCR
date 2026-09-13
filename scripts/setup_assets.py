#!/usr/bin/env python3
"""Download pinned public assets; no Python inference dependencies are needed."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import time

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
    parser.add_argument("--formula", action="store_true", help="also install the optional Pix2Text formula models (about 120 MB)")
    parser.add_argument("--progress-json", action="store_true", help="emit JSON progress events for the desktop downloader")
    args = parser.parse_args()
    def report(event, **values):
        if args.progress_json:
            print(json.dumps(dict(event=event, **values)), flush=True)
        elif "message" in values:
            print(values["message"], flush=True)
    manifest = json.loads((ROOT / "models.lock.json").read_text())
    if args.formula:
        manifest["files"] += json.loads((ROOT / "formula.lock.json").read_text())["files"]
    missing = []
    for item in manifest["files"]:
        path = ASSETS / item["path"]
        if path.is_file() and digest(path) == item["sha256"]:
            report("cached", message=f"Verified cached {item['path']}")
            continue
        missing.append(item)
    if args.offline and missing:
        raise RuntimeError("Missing or invalid cached assets: " + ", ".join(item["path"] for item in missing))
    if missing and not shutil.which("curl"):
        raise RuntimeError("curl is required to download assets. Install curl and rerun this script.")
    for item in missing:
        path = ASSETS / item["path"]
        path.parent.mkdir(parents=True, exist_ok=True)
        report("download", message=f"Downloading {item['path']}")
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False, suffix=".part") as stream:
            temporary = Path(stream.name)
        try:
            process = subprocess.Popen(["curl", "--silent", "--show-error", *(["--proxy", args.proxy] if args.proxy else []),
                            "-L", "--http1.1", "--fail", "--retry", "3", "--retry-all-errors", "--connect-timeout", "20",
                            "--max-time", "600", item["url"], "-o", str(temporary)])
            try:
                while process.poll() is None:
                    report("progress", file=item["path"], received=temporary.stat().st_size,
                           total=item.get("size", 0))
                    time.sleep(0.2)
                if process.returncode:
                    raise RuntimeError(f"Download failed ({process.returncode}): {item['path']}")
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
            if digest(temporary) != item["sha256"]:
                raise RuntimeError(f"SHA256 mismatch: {item['path']}")
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
    archive = ASSETS / "onnxruntime-linux-x64-1.30.0.tgz"
    # Never truncate a shared library mapped by a running OCR service.
    with tempfile.TemporaryDirectory(dir=ASSETS, prefix="runtime-") as staging:
        with tarfile.open(archive) as bundle:
            bundle.extractall(staging, filter="data")
        for source in Path(staging).rglob("*"):
            destination = ASSETS / source.relative_to(staging)
            if source.is_dir() and not source.is_symlink():
                destination.mkdir(parents=True, exist_ok=True)
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                source.replace(destination)
    report("ready", message="Ready: PP-OCRv6 small + ONNX Runtime CPU 1.30.0" +
           (" + Pix2Text MFR 1.5" if args.formula else ""))


if __name__ == "__main__":
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError, tarfile.TarError) as error:
        print(f"Asset setup failed: {error}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nDownload interrupted. Rerun this script to retry.", file=sys.stderr)
        sys.exit(130)

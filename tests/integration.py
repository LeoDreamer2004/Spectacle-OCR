#!/usr/bin/env python3
"""Exercise injection protocol handling and failure recovery without OCR models."""
import os
import argparse
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
ASAN = None
MAGIC = b"SOCR0001"
TEXT = "中文协议测试\nUTF-8 protocol test"


def host(path, expected=TEXT, preload=True, extra=None):
    env = os.environ.copy()
    env.pop("LD_PRELOAD", None)
    env["SPECTACLE_OCR_SOCKET"] = str(path)
    env["SPECTACLE_OCR_CONFIG"] = str(path.parent / "settings.rc")
    env["SPECTACLE_OCR_TIMEOUT_MS"] = "3000"
    if preload:
        env["LD_PRELOAD"] = (f"{ASAN}:" if ASAN else "") + str(BUILD / "libspectacle_ocr_hook.so")
    if ASAN:
        # Spectacle's existing scalar delete violates the Tesseract API; memory
        # checks use its documented delete[] contract, without suppressing ASan.
        env["SPECTACLE_OCR_TEST_DOCUMENTED_DELETE"] = "1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    env.update(extra or {})
    result = subprocess.run([BUILD / "ocr-host", expected], env=env,
                            capture_output=True, text=True, timeout=25)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stderr


def exact(stream, count):
    data = b""
    while len(data) < count:
        chunk = stream.recv(count - len(data))
        if not chunk:
            raise EOFError("truncated request")
        data += chunk
    return data


def fake_service(path, reply, verify=False, delay=0):
    listener = socket.socket(socket.AF_UNIX)
    listener.bind(str(path))
    listener.listen()
    listener.settimeout(20)
    failures = []

    def run():
        try:
            for _ in range(5):
                with listener.accept()[0] as stream:
                    stream.settimeout(5)
                    header = exact(stream, 20)
                    width, height, length = struct.unpack("<III", header[8:])
                    pixels = exact(stream, length)
                    if verify:
                        assert header[:8] == MAGIC
                        assert (width, height, length) == (33, 32, 33 * 32 * 3)
                        assert pixels == b"\xff" * length, "row padding leaked"
                    if delay:
                        time.sleep(delay)
                    try:
                        stream.sendall(reply)
                    except BrokenPipeError:
                        pass
        except Exception as error:
            failures.append(error)
        finally:
            listener.close()

    worker = threading.Thread(target=run, daemon=True)
    worker.start()
    return worker, failures


def main():
    with tempfile.TemporaryDirectory(prefix="socr-test-") as temp:
        temp = Path(temp)
        assert "using Tesseract" in host(temp / "missing.sock", "")
        host(temp / "missing.sock", "", preload=False)
        host(temp / "missing.sock", "", extra={"SPECTACLE_OCR_SOCKET": ""})
        print("PASS: absent service fallback, no preload, disabled hook")

        cases = [
            ("rgb", MAGIC + struct.pack("<II", 0, len(TEXT.encode())) + TEXT.encode(), TEXT, True, 0),
            ("empty", MAGIC + struct.pack("<II", 0, 0), "", False, 0),
            ("bad-magic", b"INVALID!" + struct.pack("<II", 0, 0), "", False, 0),
            ("oversize", MAGIC + struct.pack("<II", 0, 1024 * 1024 + 1), "", False, 0),
            ("failure", MAGIC + struct.pack("<II", 1, 0), "", False, 0),
            ("truncated", MAGIC, "", False, 0),
            ("nul", MAGIC + struct.pack("<II", 0, 3) + b"a\0b", "", False, 0),
            ("timeout", b"", "", False, 3.1),
        ]
        for name, reply, expected, verify, delay in cases:
            path = temp / f"{name}.sock"
            worker, failures = fake_service(path, reply, verify, delay)
            stderr = host(path, expected)
            worker.join(timeout=5)
            assert not worker.is_alive(), name
            assert not failures, failures
            if name not in ("rgb", "empty"):
                assert "using Tesseract" in stderr, stderr
            print(f"PASS: {name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--asan", action="store_true")
    args = parser.parse_args()
    if args.asan:
        BUILD = ROOT / "build/asan"
        ASAN = subprocess.check_output(["c++", "-print-file-name=libasan.so"], text=True).strip()
    main()

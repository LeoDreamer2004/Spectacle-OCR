#!/usr/bin/env python3
"""Real model + socket + preloaded Tesseract host integration. Requires assets/Pillow."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time

from fixtures import LINES, generate
from integration import exact, MAGIC

ROOT = Path(__file__).resolve().parents[1]
SERVICE = ROOT / "target/release/spectacle-ocr-service"


def request(path, image):
    with socket.socket(socket.AF_UNIX) as stream:
        stream.settimeout(20)
        stream.connect(str(path))
        pixels = image.tobytes()
        stream.sendall(MAGIC + struct.pack("<III", *image.size, len(pixels)) + pixels)
        header = exact(stream, 16)
        assert header[:8] == MAGIC
        status, length = struct.unpack("<II", header[8:])
        assert length <= 1024 * 1024
        text = exact(stream, length).decode()
        assert status == 0, text
        return text


def main():
    expected = "\n".join(LINES)
    fixtures = ROOT / "build/fixtures"
    images = generate(fixtures)
    with tempfile.TemporaryDirectory(prefix="socr-model-") as temporary:
        temporary = Path(temporary)
        path = temporary / "ocr.sock"
        config = temporary / "settings.rc"
        service_env = dict(os.environ, SPECTACLE_OCR_CONFIG=str(config))
        with (temporary / "service.log").open("w+") as log:
            service = subprocess.Popen([SERVICE, "--socket", path, "--assets", ROOT / "assets"], stderr=log, env=service_env)
            try:
                for _ in range(3000):
                    if path.exists(): break
                    if service.poll() is not None:
                        log.seek(0)
                        raise AssertionError(log.read())
                    time.sleep(0.01)
                assert path.exists(), "model startup timed out"
                # A malformed client must not kill the real OCR service.
                with socket.socket(socket.AF_UNIX) as stream:
                    stream.connect(str(path))
                    stream.sendall(b"INVALID!" + struct.pack("<III", 1, 1, 3))
                duplicate = subprocess.run([SERVICE, "--socket", path, "--assets", ROOT / "assets"],
                                           capture_output=True, text=True, timeout=20)
                assert duplicate.returncode != 0, "existing service endpoint was replaced"
                for name, image in images.items():
                    start = time.monotonic()
                    text = request(path, image)
                    assert text == ("" if name == "blank" else expected), f"{name}: {text!r}"
                    print(f"PASS: {name}, {(time.monotonic()-start)*1000:.0f}ms", flush=True)
                # Alternate real text and blank input with the same model sessions.
                assert request(path, images["text"]) == expected
                assert request(path, images["blank"]) == ""
                config.write_text("[OCR]\nThreads=2\nMaxSide=1024\n")
                assert request(path, images["text"]) == expected
                print("PASS: settings reload without restarting service", flush=True)
                env = dict(os.environ, SPECTACLE_OCR_SOCKET=str(path),
                           SPECTACLE_OCR_CONFIG=str(config),
                           SPECTACLE_OCR_TIMEOUT_MS="20000",
                           LD_PRELOAD=str(ROOT / "build/libspectacle_ocr_hook.so"))
                result = subprocess.run([ROOT / "build/ocr-host", expected, fixtures / "text.ppm"],
                                        env=env, capture_output=True, text=True, timeout=30)
                assert result.returncode == 0, result.stdout + result.stderr
                assert result.stderr.count("service returned 3 lines") == 5, result.stderr
                assert "using Tesseract" not in result.stderr, result.stderr
                print("PASS: real image → injected Tesseract API → Rust/ONNX → iterator text (5 repeats)", flush=True)
                config.write_text("[OCR]\nEnabled=false\nThreads=2\nMaxSide=1024\n")
                result = subprocess.run([ROOT / "build/ocr-host", ""], env=env, capture_output=True, text=True, timeout=20)
                assert result.returncode == 0 and "service returned" not in result.stderr, result.stderr
                config.write_text("[OCR]\nEnabled=true\nThreads=2\nMaxSide=1024\n")
                result = subprocess.run([ROOT / "build/ocr-host", expected, fixtures / "text.ppm"], env=env, capture_output=True, text=True, timeout=20)
                assert result.returncode == 0 and "service returned" in result.stderr, result.stderr
                print("PASS: settings switch native Tesseract / PP-OCR without restarting", flush=True)
            finally:
                service.terminate()
                service.wait(timeout=5)
                log.seek(0)
                (ROOT / "build/model-test.log").write_text(log.read())
    result = subprocess.run([SERVICE, "--image", fixtures / "text.png", "--assets", ROOT / "assets", "--repeat", "2"],
                            capture_output=True, text=True, timeout=20)
    assert result.returncode == 0 and result.stdout.strip() == expected, result.stderr + result.stdout
    print("PASS: image CLI and resident-session repetition", flush=True)
    with tempfile.TemporaryDirectory(prefix="socr-corrupt-") as temporary:
        temporary = Path(temporary)
        (temporary / "models").mkdir()
        (temporary / "models/det.onnx").write_bytes(b"invalid model")
        result = subprocess.run([SERVICE, "--image", fixtures / "text.png", "--assets", temporary],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode != 0 and "SHA256 mismatch" in result.stderr, result.stderr
    print("PASS: corrupted model rejected before runtime loading", flush=True)
    with tempfile.TemporaryDirectory(prefix="socr-launch-") as temporary:
        env = dict(os.environ, XDG_RUNTIME_DIR=temporary, QT_QPA_PLATFORM="offscreen")
        result = subprocess.run([ROOT / "scripts/launch.sh", "--help"],
                                env=env, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "PP-OCRv6 small CPU ready" in result.stderr, result.stderr
        assert not list(Path(temporary).iterdir()), "launcher leaked its runtime directory"
    print("PASS: launcher model readiness and exit cleanup (offscreen --help)", flush=True)


if __name__ == "__main__":
    main()

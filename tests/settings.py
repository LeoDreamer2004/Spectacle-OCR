#!/usr/bin/env python3
"""Exercise the real KDE dialog manager with the settings injection loaded."""
import os
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--asan", action="store_true")
args = parser.parse_args()
build = ROOT / ("build/asan" if args.asan else "build")
preload = str(build / "libspectacle_ocr_hook.so")
if args.asan:
    preload = subprocess.check_output(["c++", "-print-file-name=libasan.so"], text=True).strip() + ":" + preload
with tempfile.TemporaryDirectory(prefix="socr-settings-") as temporary:
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", XDG_CONFIG_HOME=temporary,
               QT_IM_MODULE="compose",  # Isolate the dialog test from the desktop's Fcitx process.
               XDG_RUNTIME_DIR=temporary, SPECTACLE_OCR_CONFIG=f"{temporary}/spectacle-ocrrc",
               SPECTACLE_OCR_SOCKET=f"{temporary}/unused.sock",
               LD_PRELOAD=preload, UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    result = subprocess.run([build / "settings-host", build / "settings-page.png"], env=env, capture_output=True, text=True, timeout=20)
    (build / "settings-test.log").write_text(result.stdout + result.stderr)
    print(result.stdout, end="")
    if result.returncode:
        raise RuntimeError(result.stderr[-3000:])

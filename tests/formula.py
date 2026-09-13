#!/usr/bin/env python3
"""Real formula CLI and text/formula socket routing; generates its own formula fixture."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
from PIL import Image
from integration import exact

ROOT = Path(__file__).resolve().parents[1]
SERVICE = ROOT / 'target/release/spectacle-ocr-service'

def fixture():
    path = ROOT / 'build/fixtures/formula.png'
    path.parent.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault('MPLCONFIGDIR', str(ROOT / 'build/matplotlib'))
    from matplotlib.mathtext import math_to_image
    math_to_image(r'$x = \frac{a}{b}$', str(path), dpi=220, format='png')
    image = Image.open(path).convert('RGB')
    return path, image

def valid(text):
    compact = ''.join(text.split())
    assert r'\frac' in compact and '{a}' in compact and '{b}' in compact and 'x' in compact, text

if __name__ == '__main__':
    path, image = fixture()
    start = time.monotonic()
    result = subprocess.run([SERVICE, '--image', path, '--formula', '--repeat', '2'], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    valid(result.stdout)
    print(f'PASS: generated fraction → LaTeX, two CLI recognitions in {time.monotonic()-start:.2f}s')
    with tempfile.TemporaryDirectory(prefix='socr-formula-') as folder:
        socket_path = Path(folder) / 'ocr.sock'
        env = dict(os.environ, SPECTACLE_OCR_CONFIG=str(Path(folder) / 'config'))
        service = subprocess.Popen([SERVICE, '--socket', socket_path, '--assets', ROOT/'assets'], env=env, stderr=subprocess.DEVNULL)
        try:
            for _ in range(500):
                if socket_path.exists(): break
                assert service.poll() is None
                time.sleep(.02)
            def request(magic, image):
                with socket.socket(socket.AF_UNIX) as stream:
                    stream.settimeout(30)
                    stream.connect(str(socket_path))
                    data = image.tobytes()
                    stream.sendall(magic + struct.pack('<III', *image.size, len(data)) + data)
                    header = exact(stream, 16)
                    assert header[:8] == b'SOCR0001'
                    status, size = struct.unpack('<II', header[8:])
                    assert size < 1024 * 1024
                    text = exact(stream, size).decode()
                    assert status == 0, text
                    return text
            for _ in range(2):
                valid(request(b'SOCRF001', image))
                assert request(b'SOCR0001', Image.new('RGB', (100, 100), 'white')) == ''
            print('PASS: formula/text requests alternate without mode leaking or service restart')
        finally:
            service.terminate()
            service.wait(timeout=5)

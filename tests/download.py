#!/usr/bin/env python3
"""Run the real download dialog/script against local file URLs, without network access."""
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='socr-download-') as folder:
    root = Path(folder)
    (root/'scripts').mkdir()
    shutil.copy(ROOT/'scripts/setup_assets.py', root/'scripts/setup_assets.py')
    source = root/'source'
    source.write_bytes(b'corrupt-first-download')
    archive = root/'runtime.tgz'
    with tarfile.open(archive, 'w:gz') as tar:
        item = tarfile.TarInfo('onnxruntime-linux-x64-1.30.0/lib/libonnxruntime.so')
        item.size = 7
        tar.addfile(item, io.BytesIO(b'runtime'))
    (root/'models.lock.json').write_text(json.dumps({'files':[dict(path='onnxruntime-linux-x64-1.30.0.tgz',url=archive.as_uri(),sha256=hashlib.sha256(archive.read_bytes()).hexdigest())]}))
    files = [dict(path='formula/'+name,url=source.as_uri(),sha256=hashlib.sha256(b'verified-formula').hexdigest(),size=16)
             for name in ('encoder_model.onnx','decoder_model.onnx','tokenizer.json')]
    (root/'formula.lock.json').write_text(json.dumps(dict(files=files)))
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_IM_MODULE='compose', QT_FORCE_STDERR_LOGGING='1',
               SPECTACLE_OCR_ROOT=str(root), SPECTACLE_OCR_CONFIG=str(root/'config'),
               SOCR_DOWNLOAD_SOURCE=str(source), SOCR_DOWNLOAD_SCREENSHOT=str(ROOT/'build/download-ui.png'))
    result = subprocess.run([ROOT/'build/download-host'], env=env, capture_output=True, text=True, timeout=30)
    (ROOT/'build/download-test.log').write_text(result.stdout+result.stderr)
    assert result.returncode == 0, result.stdout+result.stderr
    print(result.stdout, end='')

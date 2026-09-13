#!/usr/bin/env python3
"""Offline/corrupt downloads and safe runtime replacement using local fixtures."""
import contextlib
import hashlib
import importlib.util
import io
import json
import mmap
import os
from pathlib import Path
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('setup_assets', ROOT/'scripts/setup_assets.py')
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)
with tempfile.TemporaryDirectory(prefix='socr-assets-test-') as folder:
    root = Path(folder)
    assets = root/'assets'
    assets.mkdir()
    runtime = assets/'onnxruntime-linux-x64-1.30.0/lib'
    runtime.mkdir(parents=True)
    library = runtime/'libonnxruntime.so'
    library.write_bytes(b'old-runtime')
    archive = assets/'onnxruntime-linux-x64-1.30.0.tgz'
    with tarfile.open(archive, 'w:gz') as tar:
        member = tarfile.TarInfo('onnxruntime-linux-x64-1.30.0/lib/libonnxruntime.so')
        member.size = len(b'new-runtime')
        tar.addfile(member, io.BytesIO(b'new-runtime'))
    good = b'verified-model'
    model = assets/'model.onnx'
    model.write_bytes(good)
    item = dict(path='model.onnx', sha256=hashlib.sha256(good).hexdigest(), url='https://unused.invalid/model')
    manifest = dict(files=[item,dict(path=archive.name,sha256=setup.digest(archive),url='https://unused.invalid/runtime')])
    (root/'models.lock.json').write_text(json.dumps(manifest))
    setup.ROOT, setup.ASSETS = root, assets
    sys.argv = ['setup_assets.py', '--offline', '--progress-json']
    with library.open('rb') as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as mapped:
        with contextlib.redirect_stdout(io.StringIO()) as output: setup.main()
        assert mapped[:] == b'old-runtime', 'running process mapping was modified'
        assert library.read_bytes() == b'new-runtime'
        assert json.loads(output.getvalue().splitlines()[-1])['event'] == 'ready'
    model.write_bytes(b'old-corrupt-model')
    try: setup.main()
    except RuntimeError as error: assert 'Missing or invalid' in str(error)
    else: raise AssertionError('offline mode accepted a corrupt asset')
    # A local curl stand-in writes bad bytes; checksum failure must preserve the destination.
    executable = root/'curl'
    executable.write_text('#!/usr/bin/env python3\nimport sys\nfrom pathlib import Path\nPath(sys.argv[-1]).write_bytes(b"bad-download")\n')
    executable.chmod(0o755)
    os.environ['PATH'] = str(root)+os.pathsep+os.environ['PATH']
    sys.argv = ['setup_assets.py', '--proxy', 'http://127.0.0.1:7890']
    try: setup.main()
    except RuntimeError as error: assert 'SHA256 mismatch' in str(error)
    else: raise AssertionError('accepted a corrupt download')
    assert model.read_bytes() == b'old-corrupt-model'
    assert not list(assets.glob('*.part'))
print('PASS: offline validation, checksum failure preserves files, partial cleanup, mapped runtime remains intact')

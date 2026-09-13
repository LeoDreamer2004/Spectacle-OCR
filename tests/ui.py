#!/usr/bin/env python3
"""Drive the real Spectacle QML toolbar on an offscreen, generated formula image."""
import os
from pathlib import Path
import subprocess
import tempfile
import argparse
import time
from formula import fixture

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--failure', action='store_true')
args = parser.parse_args()
image, _ = fixture()
with tempfile.TemporaryDirectory(prefix='socr-ui-') as folder:
    folder = Path(folder)
    (folder/'spectaclerc').write_text('[General]\nocrLanguages=chi_sim\n')
    socket = folder / 'ocr.sock'
    env = dict(os.environ, SPECTACLE_OCR_CONFIG=str(folder/'spectacle-ocrrc'))
    assets = ROOT/'assets'
    if args.failure:
        assets = folder/'assets'
        assets.mkdir()
        for name in ('models', 'onnxruntime-linux-x64-1.30.0'):
            (assets/name).symlink_to(ROOT/'assets'/name, target_is_directory=True)
        env['SOCR_UI_EXPECT_ERROR'] = '1'
    with (ROOT/'build/ui-service.log').open('w') as log:
        service = subprocess.Popen([ROOT/'target/release/spectacle-ocr-service', '--socket', socket, '--assets', assets], env=env, stderr=log)
        try:
            for _ in range(500):
                if socket.exists(): break
                assert service.poll() is None, 'service failed; see build/ui-service.log'
                time.sleep(.02)
            env.update(QT_QPA_PLATFORM='offscreen', QT_IM_MODULE='compose', QT_FORCE_STDERR_LOGGING='1',
                       XDG_CONFIG_HOME=str(folder), XDG_CACHE_HOME=str(folder/'cache'),
                       SPECTACLE_OCR_ROOT=str(ROOT), SPECTACLE_OCR_SOCKET=str(socket),
                       SOCR_UI_SCREENSHOT=str(ROOT/'build/formula-ui.png'),
                       LD_PRELOAD=f'{ROOT}/build/libspectacle_ocr_hook.so:{ROOT}/build/libui-probe.so')
            result = subprocess.run(['spectacle', '--new-instance', '--edit-existing', str(image)], env=env,
                                    capture_output=True, text=True, timeout=50)
            (ROOT/'build/ui-test.log').write_text(result.stdout+result.stderr)
            assert result.returncode == 0, result.stdout+result.stderr
            assert 'PASS:' in result.stdout, result.stdout+result.stderr
            print(result.stdout, end='')
        finally:
            service.terminate()
            service.wait(timeout=5)

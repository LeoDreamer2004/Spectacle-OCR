#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
service="$root/target/release/spectacle-ocr-service"
hook="$root/build/libspectacle_ocr_hook.so"
if [[ ! -x "$service" || ! -f "$hook" ]]; then
    echo 'Run bash scripts/build.sh first.' >&2
    exit 1
fi
version=$(QT_QPA_PLATFORM=offscreen spectacle --version 2>/dev/null)
if [[ "$version" != 'spectacle 6.7.5' ]]; then
    echo "Unsupported Spectacle version: $version (prototype requires 6.7.5)" >&2
    exit 1
fi
if [[ $(pkg-config --modversion tesseract) != '5.5.3' ]]; then
    echo 'Prototype requires Tesseract 5.5.3; rebuild and revalidate after upgrading.' >&2
    exit 1
fi
runtime=$(mktemp -d "${XDG_RUNTIME_DIR:-/tmp}/spectacle-ocr.XXXXXXXX")
service_pid=''
cleanup() {
    if [[ -n "$service_pid" ]]; then
        kill "$service_pid" 2>/dev/null || true
        wait "$service_pid" 2>/dev/null || true
    fi
    rm -f -- "$runtime/ocr.sock"
    rmdir -- "$runtime"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
"$service" --socket "$runtime/ocr.sock" --assets "$root/assets" &
service_pid=$!
for ((attempt=0; attempt<3000; attempt++)); do
    [[ -S "$runtime/ocr.sock" ]] && break
    kill -0 "$service_pid" 2>/dev/null || { echo 'Service exited.' >&2; exit 1; }
    sleep 0.02
done
[[ -S "$runtime/ocr.sock" ]] || { echo 'Service startup timed out.' >&2; exit 1; }
echo 'PP-OCRv6 small CPU ready. The OCR button now uses the real model.' >&2
# A new instance avoids forwarding this invocation to an un-injected DBus owner.
SPECTACLE_OCR_SOCKET="$runtime/ocr.sock" LD_PRELOAD="$hook${LD_PRELOAD:+:$LD_PRELOAD}" \
    spectacle --new-instance "$@"

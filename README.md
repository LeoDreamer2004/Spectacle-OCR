# SpectacleOCR

**English** | [简体中文](README.zh-CN.md)

SpectacleOCR provides Chinese and English text recognition, an integrated settings page,
configuration reloads, and fallback to Tesseract. It currently targets **Spectacle 6.7.5**
and **Tesseract 5.5.3**, using CPU inference.

## Quick start

Complete [Building from source](#building-from-source) first. Then run from the project directory:

```sh
./scripts/launch.sh --launchonly
```

Take a screenshot in the new Spectacle window and click its OCR button.
The launcher loads and warms up the models before opening the window. Model sessions are
reused for subsequent screenshots in that instance. Closing the window stops the service
and removes its temporary socket. `--new-instance` prevents forwarding to an existing,
unmodified Spectacle process.

## Application menu and screenshot shortcuts

To launch SpectacleOCR from your application menu, create a
separate user entry at `~/.local/share/applications/spectacle-ocr.desktop`.

Replace the `Exec` path below with the **absolute path** to your checkout:

```ini
[Desktop Entry]
Type=Application
Name=SpectacleOCR
Comment=Screenshot text recognition with local PP-OCR
Comment[zh_CN]=使用本地 PP-OCR 识别截图文字
Exec="/absolute/path/to/SpectacleOCR/scripts/launch.sh" --launchonly
Icon=spectacle
Terminal=false
DBusActivatable=false
Categories=Utility;
```

`Exec` is not a regular shell command: do not use `~` or `$HOME` instead of an absolute
path. Update it if you move the checkout. A separate entry keeps the original Spectacle
menu item available.

**Global shortcuts such as Print need separate configuration.** Adding a menu entry does
not automatically change existing shortcut bindings. In KDE's shortcut settings, bind
the desired shortcut to the SpectacleOCR entry or a command such as:

```sh
/absolute/path/to/SpectacleOCR/scripts/launch.sh --region
```

Remove any conflicting old binding. The current launcher loads models before opening a
new Spectacle instance, so region selection starts after warmup.

## Building from source

The current target is Linux x86_64 with **Spectacle 6.7.5** and **Tesseract 5.5.3**.
Install Rust/Cargo, a C++17 compiler, CMake, pkg-config, and development files for
Tesseract, Qt6 Widgets, and KF6 ConfigWidgets. Asset setup also needs Python 3.12+ and curl.
Keep at least one Tesseract recognition language installed; tests default to `chi_sim`.
Inference does not require Python, the PaddleOCR Python package, or a system installation
of ONNX Runtime.

```sh
python3 scripts/setup_assets.py
./scripts/build.sh
./scripts/launch.sh --launchonly
```

`models.lock.json` pins official model revisions, URLs, and SHA256 checksums.

The setup script downloads about 43 MB, verifies SHA256 checksums, and extracts the runtime
into `assets/` (about 70 MiB total, excluded from Git). Verified cached files are reused.
Run `python3 scripts/setup_assets.py --help` for options.

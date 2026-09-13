# SpectacleOCR

[English](README.md) | **简体中文**

为 KDE Spectacle 接入本地 PP-OCR 后端。

支持本地中英文识别、原生设置页、参数热更新及 Tesseract 故障回退。
当前适配 Spectacle 6.7.5 / Tesseract 5.5.3，推理使用 CPU。

## 直接使用

首次使用请先完成下方的[从头构建](#从头构建)。构建完成后，从项目目录运行：

```sh
./scripts/launch.sh --launchonly
```

启动器等待模型加载、预热完成才打开窗口；同一窗口连续截图复用模型会话。
关闭窗口后停止服务并清理临时 socket。使用 `--new-instance` 避免转交给未注入的旧实例。

## 公式识别

截图后，点击 OCR 按钮旁的箭头，选择 **公式识别 → LaTeX**，再点击“公式识别”。
请框选单个公式；结果沿用 Spectacle 的复制与提示流程，输出 LaTeX 源码，不渲染公式。
识别期间按钮暂时禁用，结束后可切回文本模式。

首次使用会提示下载约 120 MB 的 Pix2Text MFR 1.5 模型，支持进度显示、取消和失败重试。
也可以提前运行 `python3 scripts/setup_assets.py --formula`；下载地址与 SHA256 固定在
[formula.lock.json](formula.lock.json)。公式模型按需加载，普通截图启动不加载它。
设置页可调整启动时默认模式，并下载或校验公式模型。

公式识别使用 CPU，单次请求上限 25 秒；失败时提示错误，不回退为普通文字识别。
目前不支持自动分离整页中的正文与公式。

## 应用菜单与截图快捷键

若希望从应用菜单使用 SpectacleOCR，建议新增用户级入口：
`~/.local/share/applications/spectacle-ocr.desktop`。

将下面 `Exec` 中的路径替换为你本机项目的**绝对路径**：

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

`Exec` 不是普通 shell 命令，不要使用 `~` 或 `$HOME` 代替绝对路径。项目移动后，
需要同步更新此路径。新增入口保留原来的 Spectacle 菜单项，便于分别使用。

**Print 等全局截图快捷键需要单独检查。** 新增菜单项不会自动改写原有快捷键。
在 KDE 的快捷键设置中，把需要的组合键绑定到 SpectacleOCR 入口或如下命令，
并解除冲突的旧绑定：

```sh
/absolute/path/to/SpectacleOCR/scripts/launch.sh --region
```

当前启动器每次都会先加载模型，并打开独立 Spectacle 实例；模型预热前不会出现框选界面。

## 从头构建

当前针对 Linux x86_64、Spectacle **6.7.5**、Tesseract **5.5.3**。
需要 Rust/Cargo、C++17 编译器、CMake、pkg-config、Tesseract、Qt6 Widgets/Quick/Qml 和 KF6 ConfigWidgets 开发文件，以及
Python 3.12+ 和 curl（仅用于资源下载）。仍需保留 Spectacle 原有 Tesseract
语言数据，测试默认使用 `chi_sim`。推理本身不需要 Python、PaddleOCR Python 包或系统安装 ONNX Runtime。

```sh
python3 scripts/setup_assets.py
./scripts/build.sh
./scripts/launch.sh --launchonly
```

`models.lock.json` 固定官方模型的提交、下载链接和 SHA256。

下载脚本自动下载约 43 MB 的资源、校验 SHA256 并解压运行库，放入被 Git 忽略的
`assets/`（总占用约 70 MiB）；已通过校验的文件会复用。
运行 `python3 scripts/setup_assets.py --help` 查看选项。

公式测试：`python3 tests/formula.py`；真实 Spectacle 离屏界面测试：`python3 tests/ui.py`。
两者需要公式模型、Pillow、Matplotlib，界面测试还需要 `chi_sim` 语言数据。

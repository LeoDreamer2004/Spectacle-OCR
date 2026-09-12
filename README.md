# SpectacleOCR

为 KDE Spectacle 接入本地 PP-OCR 后端，保留原生 OCR 按钮。

支持本地中英文识别、原生设置页、参数热更新及 Tesseract 故障回退。
当前适配 Spectacle 6.7.5 / Tesseract 5.5.3，推理使用 CPU。

```text
Spectacle 6.7.5
  → C++ LD_PRELOAD 注入库
  → Unix socket（RGB888 → UTF-8）
  → Rust 常驻服务
  → ONNX Runtime CPU → PP-OCRv6 small 检测 / 识别
```

## 直接使用

首次使用请先完成下方的[从头构建](#从头构建)。构建完成后，从项目目录运行：

```sh
./scripts/launch.sh --launchonly
```

在新开的 Spectacle 窗口截图并点击原生 OCR 按钮，现在返回真实识别结果。
启动器等待模型加载、预热完成才打开窗口；同一窗口连续截图复用模型会话。
关闭窗口后停止服务并清理临时 socket。使用 `--new-instance` 避免转交给未注入的旧实例。

## Spectacle 内的设置页

重新用 `./scripts/launch.sh --launchonly` 启动，打开 Spectacle 的配置窗口，
左侧会新增 **SpectacleOCR** 页面：

- 启用开关：勾选使用 PP-OCR；取消勾选使用原生 Tesseract。
- CPU 线程数：1–32，默认 4。
- 检测长边上限：320–2048 像素，默认 1536。
- 请求超时：100–25000 毫秒，默认 20000。

沿用原有的应用、确定、取消、恢复默认值按钮。保存后的配置从下一次识别开始生效；
修改线程数或检测分辨率时会重建模型会话，不需要重启 Spectacle。当前正在进行的
识别继续使用原参数。关闭 PP-OCR 开关不会停止后台服务，以便随时切回。

配置保存在 `$XDG_CONFIG_HOME/spectacle-ocrrc`，未设置 XDG 路径时为
`~/.config/spectacle-ocrrc`，与 Spectacle 自身配置分开。可用
`SPECTACLE_OCR_CONFIG` 指定其他路径，启动器和服务会共用该路径。
配置文件使用 KDE INI 格式：

```ini
[OCR]
Enabled=true
Threads=4
MaxSide=1536
TimeoutMs=20000
```

## 从头构建

当前针对 Linux x86_64、Spectacle **6.7.5**、Tesseract **5.5.3**。
需要 Rust/Cargo、C++17 编译器、CMake、pkg-config、Tesseract、Qt6 Widgets 和 KF6 ConfigWidgets 开发文件，以及
Python 3.12+ 和 curl（仅用于资源下载）。仍需保留 Spectacle 原有 Tesseract
语言数据，测试默认使用 `chi_sim`。推理本身不需要 Python、PaddleOCR Python 包或系统安装 ONNX Runtime。

```sh
python3 scripts/setup_assets.py
./scripts/build.sh
./scripts/launch.sh --launchonly
```

`models.lock.json` 固定官方模型的提交、下载链接和 SHA256。脚本下载约 43 MB，
模型和独立运行库均放入 `assets/`，总占用约 70 MB；已有文件先校验后复用。
下载采用 HTTP/1.1 和断连重试，校验成功后才替换目标文件。
Rust 服务启动时再次校验模型及字典，不会在识别时下载文件。

**Git 仓库不包含上述下载资源。** 完整清单、固定下载地址、SHA256、代理与离线准备
见 [docs/downloads.md](docs/downloads.md)。`Cargo.lock` 与 `models.lock.json` 均纳入版本控制。

## 图片命令与配置

不启动 Spectacle 也可以识别 PNG、JPEG、PPM：

```sh
target/release/spectacle-ocr-service --image screenshot.png --assets assets
```

结果写入标准输出；模型加载时间、图片尺寸、检测区域数及耗时写入标准错误，
不记录图片像素或识别文字。重复测试复用同一组会话，标准输出只打印最后一次结果：

```sh
target/release/spectacle-ocr-service --image screenshot.png --repeat 3
```

CPU 默认 4 线程、检测长边上限 1536。可在 SpectacleOCR 设置页调整；
独立图片命令也可覆盖保存的参数：

```sh
target/release/spectacle-ocr-service --image screenshot.png --threads 6 --det-max-side 2048
```

显式 CLI 参数优先于配置文件；`--config FILE` 可指定配置路径。
旧的 `SPECTACLE_OCR_THREADS` 和 `SPECTACLE_OCR_DET_MAX_SIDE` 启动环境变量已由配置页取代。
手动设置 `SPECTACLE_OCR_TIMEOUT_MS` 时，该值优先于页面中的超时。
截图超过检测分辨率时会缩小，小字较多可提高长边上限。
模型固定支持中英文，Spectacle 原有 Tesseract 语言选择暂不控制 PP-OCR 模型。
**当前仅提供 CPU 后端，CUDA 尚未实现。**

手动运行常驻服务时使用私有目录：

```sh
mkdir -m 700 /tmp/my-spectacle-ocr
target/release/spectacle-ocr-service --socket /tmp/my-spectacle-ocr/ocr.sock --assets assets
```

另一个终端从项目根目录运行：

```sh
SPECTACLE_OCR_SOCKET=/tmp/my-spectacle-ocr/ocr.sock \
SPECTACLE_OCR_TIMEOUT_MS=20000 \
LD_PRELOAD="$PWD/build/libspectacle_ocr_hook.so" \
spectacle --new-instance --launchonly
```

手动服务停止后留下 socket 文件，下次启动前需自行清理；不会覆盖已有 endpoint。

## 验证

```sh
cargo test
cargo clippy --all-targets -- -D warnings
python3 tests/integration.py
python3 tests/models.py
python3 tests/settings.py
```

`integration.py` 无需模型，覆盖协议、RGB 行填充、重复识别、空结果、服务缺失、
畸形/截断/过大响应、断连和超时回退。`models.py` 使用真实模型，需要 Pillow 和
Noto Sans CJK 字体；生成普通中英文、深色背景、16 像素小字及空白图，检查文字与
预期逐字匹配，并把实际图片送入预加载注入库的 C++ 宿主，连续运行 5 次。
另外检查 CLI 会话复用及损坏模型的启动拒绝。测试不读取桌面或剪贴板。

参考测试环境：Ryzen 7 5800H、CPU 4 线程、release 构建的首轮结果：

| 样本 | 结果 | 单次耗时（含 socket 交换） |
| --- | --- | --- |
| 900×240，32 px，中英文三行 | 逐字匹配 | 178 ms |
| 900×240，24 px，深色背景 | 逐字匹配 | 208 ms |
| 900×240，16 px，小字 | 逐字匹配 | 169 ms |
| 640×480，空白 | 正确返回空文本 | 148 ms |

该轮模型加载和预热 0.29 秒。以上是合成样本数据，不代表所有截图的准确率或性能。
测试图片位于 `build/fixtures/`，最近一轮服务日志位于 `build/model-test.log`。
已完成 Spectacle GUI 的真实识别验证。

`settings.py` 使用真实 KDE 配置对话框，验证页面注入、应用、取消、默认值和重复打开；
预览图写入 `build/settings-page.png`。测试使用临时配置目录，不改用户设置。
真实模型测试也覆盖参数热更新及原生/PP-OCR 切换。设置页测试使用 Qt compose
输入上下文，隔离桌面 Fcitx 插件；不改变实际启动时的输入法。

原生层内存检查使用 Tesseract 约定的 `delete[]`：

```sh
cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug -DSOCR_SANITIZE=ON
cmake --build build/asan --parallel 2
python3 tests/integration.py --asan
python3 tests/settings.py --asan
```

## 实现边界

- 针对当前 Spectacle 的 `RGB888 → Recognize(nullptr) → RIL_TEXTLINE` 调用路径，
  不是通用 Tesseract 替代库。不支持的图片输入及未配置 socket 时走原生路径。
- 注入层读取设置页保存的超时，默认 20 秒；也可通过
  `SPECTACLE_OCR_TIMEOUT_MS` 调整（100–25000）。请求失败时对原图运行 Tesseract。
  原生回退耗时不包含在这个截止时间内。
- 后端在文本行之间检查 15 秒处理预算；单次 ONNX 推理尚不能中途取消。
  服务顺序处理请求，暂未实现运行中取消和并发队列调度。
- 检测使用官方 BGR 归一化、DB 概率连通区域、边界扩展和去重；识别使用动态宽度、
  BGR 归一化和 CTC 解码。当前针对正向水平截图，未实现倾斜矫正、竖排或复杂分栏排版。
  检测框按行从上到下、同一行从左到右输出，每个框一行；低置信度结果被过滤。
- 输入上限 64 MiB RGB、输出上限 1 MiB、最多 256 个文本区域；过长文本行
  （缩放至高度 48 后宽度超过 3200）返回错误并触发原生回退，不静默截断。
- 结果迭代器通过 Tesseract 的 32×32 空白页建立有效对象，再构造真实 C++ 子类。
  仍依赖原生库及语言初始化，不手工伪造对象内存或 vtable。
- Spectacle 6.7 分支对返回字符串使用 `delete`，Tesseract 约定为 `delete[]`。
  注入层遵循 Tesseract 约定；普通测试覆盖宿主当前释放路径，sanitizer 测试使用
  正确的数组释放路径。该宿主已有问题尚未通过修改 Spectacle 修复。
- 升级后必须重新检查接口、编译和测试。当前不修改全局桌面入口或截图快捷键。

## 后续

1. 根据日常截图调整检测分辨率、文本框合并与排版。
2. 接入 CUDA 并测试 CPU 回退，比较延迟、功耗与显存。
3. 加入取消、服务管理及安装工具。

## 来源

实现独立编写，参考 screen-ocr 的 Rust / ONNX 技术路线及官方模型规格，未复制其模型流水线。

- [官方检测模型](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_det_onnx) 与
  [官方识别模型](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_rec_onnx)：Apache-2.0。
- [ONNX Runtime](https://github.com/microsoft/onnxruntime/releases/tag/v1.30.0)：MIT，发行包内保留许可证。
- [ort Rust 绑定](https://docs.rs/ort/2.0.0-rc.13/ort/)。
- [Spectacle OcrManager](https://github.com/KDE/spectacle/blob/Plasma/6.7/src/OcrManager.cpp)。
- [参考项目 screen-ocr](https://github.com/crlcrl1/screen-ocr)。

`native/` 注入库；`src/` Rust 服务和 OCR 流水线；`scripts/` 下载、构建及启动；
`tests/` 自动化验证；`docs/protocol.md` 通信协议。

## 许可证

本项目原创代码使用 [MIT License](LICENSE)。第三方依赖、模型和运行库的许可证与来源
见 [THIRD_PARTY.md](THIRD_PARTY.md)。

# 下载资源与离线准备

Git 仓库只包含源码、测试、文档和锁定清单，不包含模型、运行库或构建产物。
所有命令从项目根目录运行。

## 自动下载

需要 Python 3.12+、curl，以及访问 Hugging Face 和 GitHub 的网络：

```sh
python3 scripts/setup_assets.py
```

脚本读取仓库内的 [models.lock.json](../models.lock.json)，检查 SHA256，
复用已验证文件；缺失或校验失败的文件重新下载。新文件在校验通过后才替换目标。
HTTP/1.1、连接超时和失败重试已启用；中断后重新执行同一命令即可。
尚未完成的单个文件会重新下载，不支持断点续传。

## 文件清单

| 本地保存路径 | 大小（字节） | 官方下载地址 |
| --- | ---: | --- |
| `assets/models/det.onnx` | 9,880,512 | [固定版本下载](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_det_onnx/resolve/28fe5895c24fd108c19eb3e8479f4ab385fbfc62/inference.onnx) |
| `assets/models/rec.onnx` | 21,159,378 | [固定版本下载](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_rec_onnx/resolve/b8f84f0b80c529de40b4fbb3544b84fa7233a513/inference.onnx) |
| `assets/models/rec.yml` | 150,579 | [固定版本下载](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_rec_onnx/resolve/b8f84f0b80c529de40b4fbb3544b84fa7233a513/inference.yml) |
| `assets/models/det.yml` | 885 | [固定版本下载](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_det_onnx/resolve/28fe5895c24fd108c19eb3e8479f4ab385fbfc62/inference.yml) |
| `assets/onnxruntime-linux-x64-1.30.0.tgz` | 11,306,877 | [固定版本下载](https://github.com/microsoft/onnxruntime/releases/download/v1.30.0/onnxruntime-linux-x64-1.30.0.tgz) |

下载总量约 42.5 MB（40.5 MiB）；含解压后的运行库约占 70 MiB。
模型仓库提交、完整链接和 SHA256 均以 `models.lock.json` 为准。
检测与识别模型为官方 PP-OCRv6 small；字典来自对应识别模型的 `inference.yml`。
运行库为 Linux x86_64 的 ONNX Runtime 1.30.0 CPU 发行包，无需 CUDA。

## 手动下载或代理环境

浏览器打开上表的固定版本链接，将文件保存到对应路径。注意两个模型都叫
`inference.onnx`，需要分别命名为 `det.onnx` 与 `rec.onnx`；两个 YAML
也分别保存为 `det.yml` 与 `rec.yml`，不要相互覆盖。
然后运行下载脚本：完整且校验正确的文件不会重新请求网络，运行库会自动解压。

curl 使用标准代理环境变量，可仅为下载命令指定代理：

```sh
HTTPS_PROXY=http://127.0.0.1:7890 python3 scripts/setup_assets.py
```

校验值如下，也可用 `sha256sum` 手动核对：

```text
d73e0058b7a8086bbd57f3d10b8bcd4ff95363f67e06e2762b5e814fe9c9410e  assets/models/det.onnx
5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634  assets/models/rec.onnx
ab078671bb49f06228eadccd34f1bb501e157f7a047095ffb943ba81512c77d1  assets/models/rec.yml
193f435274bf9f0b5f71a929bbfbcf148282df7e633b34e7c373e8f44741b516  assets/models/det.yml
a5ed5a3cac51fbb2e90da632ae43d19212faaa20e76484e62bcb7c23ddb3b3fd  assets/onnxruntime-linux-x64-1.30.0.tgz
```

运行库解压后必须存在：

```text
assets/onnxruntime-linux-x64-1.30.0/lib/libonnxruntime.so
```

需要单独解压时：

```sh
tar -xzf assets/onnxruntime-linux-x64-1.30.0.tgz -C assets
```

模型和运行库位于被 Git 忽略的 `assets/` 内；不要将下载文件加入提交。
其许可证独立于本项目 MIT，见 [THIRD_PARTY.md](../THIRD_PARTY.md)。

## Rust 与系统依赖

首次构建还会从配置的 Cargo registry 下载 Rust crates，版本固定在仓库的
`Cargo.lock`。可以提前准备：

```sh
cargo fetch --locked
```

离线机器需要已有对应 crates 缓存（或自行使用 Cargo vendoring），以及已安装的
编译器、CMake、pkg-config、Tesseract、Qt6、KDE Frameworks 开发文件。
模型下载脚本不负责安装这些系统依赖，具体版本要求见 [README](../README.md#从头构建)。

Tesseract 的语言数据仍由 Spectacle 初始化和故障回退使用，至少安装一种识别语言；
测试默认使用 `chi_sim`，也可通过 `SPECTACLE_OCR_TEST_LANGUAGE` 指定已安装的其他语言。
PP-OCR 模型自身包含中英文识别能力，不使用 Tesseract 字典进行推理。

## 可选测试依赖

真实模型的合成图片测试需要 Pillow 和 Noto Sans CJK 字体。Pillow 可安装到
被 Git 忽略的虚拟环境：

```sh
python3 -m venv .venv
.venv/bin/python -m pip install Pillow
.venv/bin/python tests/models.py
```

字体通过操作系统的软件包管理器安装。当前测试使用
`/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc`；其他发行版需要调整
`tests/fixtures.py` 中的 `FONT` 路径。GUI/协议测试不依赖 Pillow。

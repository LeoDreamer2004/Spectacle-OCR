# 第三方来源与许可证

本项目原创源码以 [MIT](LICENSE) 许可发布。以下项目保留各自的许可证，
本项目的 MIT 许可不覆盖它们。模型、原生运行库和系统依赖不随此源码仓库分发。

## 模型与运行库

| 资源 | 来源 | 许可证 |
| --- | --- | --- |
| PP-OCRv6 small 检测模型及规格 | [PaddlePaddle](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_det_onnx) | Apache-2.0 |
| PP-OCRv6 small 识别模型及字典 | [PaddlePaddle](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_rec_onnx) | Apache-2.0 |
| ONNX Runtime 1.30.0 CPU | [Microsoft](https://github.com/microsoft/onnxruntime/tree/v1.30.0) | MIT |
| Pix2Text MFR 1.5 公式模型与 tokenizer | [breezedeus](https://huggingface.co/breezedeus/pix2text-mfr-1.5) | MIT（模型卡声明） |

固定版本、下载地址与校验值见 [models.lock.json](models.lock.json)，运行
`python3 scripts/setup_assets.py` 自动下载、校验并解压。
ONNX Runtime 压缩包中的 LICENSE 与第三方 notices 会保留在解压目录中。

可选公式资源固定在 [formula.lock.json](formula.lock.json)，下载时添加 `--formula`。

## 直接 Rust 依赖

以下信息来自当前 Cargo.lock 所解析的软件包元数据：

| 依赖 | 版本 | 声明的许可证 |
| --- | --- | --- |
| [image](https://crates.io/crates/image/0.25.10) | 0.25.10 | `MIT OR Apache-2.0` |
| [ort](https://crates.io/crates/ort/2.0.0-rc.13) | 2.0.0-rc.13 | `MIT OR Apache-2.0` |
| [serde](https://crates.io/crates/serde/1.0.229) | 1.0.229 | `MIT OR Apache-2.0` |
| [serde_json](https://crates.io/crates/serde_json/1.0.151) | 1.0.151 | `MIT OR Apache-2.0` |
| [serde_yaml](https://crates.io/crates/serde_yaml/0.9.34+deprecated) | 0.9.34+deprecated | `MIT OR Apache-2.0` |
| [sha2](https://crates.io/crates/sha2/0.10.9) | 0.10.9 | `MIT OR Apache-2.0` |

传递依赖的精确版本保存在 `Cargo.lock`；可用 `cargo metadata --locked --format-version 1`
检查所有依赖的软件包许可证字段。发布二进制时应同时保留所分发组件要求的许可证与 notices。

## 动态链接的系统组件

- [Tesseract](https://github.com/tesseract-ocr/tesseract)：Apache-2.0。
- [Qt6](https://www.qt.io/licensing/)：相关组件有 LGPL-3.0、GPL 或商业许可选项，
  以所使用 Qt 发行包与组件的许可为准。
- KDE Frameworks 的 [KConfig](https://invent.kde.org/frameworks/kconfig)、
  [KConfigWidgets](https://invent.kde.org/frameworks/kconfigwidgets) 和
  [KWidgetsAddons](https://invent.kde.org/frameworks/kwidgetsaddons)：组件与文件采用各自的 LGPL 等许可；
  以其 `LICENSES/`、源码 SPDX 标记和发行包说明为准。

## 技术参考

本项目独立实现了图像推理流水线，参考了
[screen-ocr](https://github.com/crlcrl1/screen-ocr) 的 Rust/ONNX 技术路线及官方模型规格；
未复制其流水线源码。Spectacle 与 Tesseract 源码用于核对宿主调用方式，
未将这两个项目的源码直接纳入此仓库。

#include "settings.h"
#include "formula_ui.h"
#include <KConfigDialog>
#include <KCoreConfigSkeleton>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QPushButton>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>

namespace {
class Configuration final : public KCoreConfigSkeleton {
public:
  bool enabled = true;
  int threads = 4, maxSide = 1536, timeoutMs = 20000, defaultMode = 0;
  explicit Configuration(QObject *parent)
      : KCoreConfigSkeleton(socr::configPath(), parent) {
    setCurrentGroup("OCR");
    addItemBool("Enabled", enabled, true);
    auto add = [this](const QString &name, int &value, int defaultValue,
                      int minimum, int maximum) {
      auto *item = addItemInt(name, value, defaultValue);
      item->setMinValue(minimum);
      item->setMaxValue(maximum);
    };
    add("Threads", threads, 4, 1, 32);
    add("MaxSide", maxSide, 1536, 320, 2048);
    add("TimeoutMs", timeoutMs, 20000, 100, 25000);
    add("DefaultMode", defaultMode, 0, 0, 1);
    load();
  }
};

QSpinBox *spin(QWidget *parent, const char *name, int minimum, int maximum,
               int step, const QString &suffix = {}) {
  auto *box = new QSpinBox(parent);
  box->setObjectName(QString("kcfg_") + name);
  box->setRange(minimum, maximum);
  box->setSingleStep(step);
  box->setSuffix(suffix);
  return box;
}
} // namespace

// Spectacle adds its native pages through this dynamically linked overload.
// Add our managed page once, after the first native page, while the dialog is
// still being assembled. No constructors or object layouts are replaced.
KPageWidgetItem *KConfigDialog::addPage(QWidget *page,
                                        KCoreConfigSkeleton *config,
                                        const QString &name,
                                        const QString &icon,
                                        const QString &header) {
  using Add =
      KPageWidgetItem *(*)(KConfigDialog *, QWidget *, KCoreConfigSkeleton *,
                           const QString &, const QString &, const QString &);
  static auto next = reinterpret_cast<Add>(
      dlsym(RTLD_NEXT, "_ZN13KConfigDialog7addPageEP7QWidgetP19KCoreConfigSkele"
                       "tonRK7QStringS6_S6_"));
  if (!next) {
    std::fprintf(stderr, "SpectacleOCR: KConfigDialog ABI unavailable\n");
    std::abort();
  }
  auto *result = next(this, page, config, name, icon, header);
  if (objectName() != "settings" ||
      qEnvironmentVariableIsEmpty("SPECTACLE_OCR_SOCKET") ||
      property("socrPageAdded").toBool())
    return result;
  setProperty("socrPageAdded", true);
  auto *settingsPage = new QWidget(this);
  settingsPage->setObjectName("spectacleOcrPage");
  auto *skeleton = new Configuration(settingsPage);
  auto *layout = new QVBoxLayout(settingsPage);
  auto *description = new QLabel(
      QStringLiteral("使用本地 PP-OCRv6 "
                     "模型识别中英文。设置保存后，从下一次识别开始生效。"),
      settingsPage);
  description->setWordWrap(true);
  layout->addWidget(description);
  auto *enabled = new QCheckBox(
      QStringLiteral("使用 SpectacleOCR（取消勾选使用原生 Tesseract）"),
      settingsPage);
  enabled->setObjectName("kcfg_Enabled");
  layout->addWidget(enabled);
  auto *options = new QWidget(settingsPage);
  auto *form = new QFormLayout(options);
  auto *mode = new QComboBox(options);
  mode->setObjectName("kcfg_DefaultMode");
  mode->addItems({QStringLiteral("文本识别"), QStringLiteral("公式识别 → LaTeX")});
  form->addRow(QStringLiteral("启动时默认模式："), mode);
  form->addRow(QStringLiteral("识别模型："),
               new QLabel("PP-OCRv6 small · CPU", options));
  form->addRow(QStringLiteral("CPU 线程数："),
               spin(options, "Threads", 1, 32, 1));
  form->addRow(
      QStringLiteral("检测长边上限："),
      spin(options, "MaxSide", 320, 2048, 32, QStringLiteral(" 像素")));
  form->addRow(
      QStringLiteral("文本请求超时："),
      spin(options, "TimeoutMs", 100, 25000, 1000, QStringLiteral(" 毫秒")));
  layout->addWidget(options);
  auto *models = new QPushButton(QStringLiteral("下载 / 校验公式模型（约 120 MB）"), settingsPage);
  models->setEnabled(!socr::FormulaUi::root().isEmpty());
  connect(models, &QPushButton::clicked, this, [] { socr::manageFormulaModels(); });
  layout->addWidget(models);
  auto *modelStatus = new QLabel(settingsPage);
  const auto refreshStatus = [modelStatus] {
    bool installed = true;
    for (const auto &file : {"encoder_model.onnx", "decoder_model.onnx", "tokenizer.json"})
      installed &= QFileInfo(socr::FormulaUi::root() + "/assets/formula/" + file).size() > 0;
    modelStatus->setText(installed ? QStringLiteral("公式模型：已下载 · 首次使用时加载") : QStringLiteral("公式模型：未安装 · 不影响文本识别"));
  };
  refreshStatus();
  auto *statusTimer = new QTimer(modelStatus);
  connect(statusTimer, &QTimer::timeout, modelStatus, refreshStatus);
  statusTimer->start(1000);
  layout->addWidget(modelStatus);
  auto *hint =
      new QLabel(QStringLiteral("提高检测分辨率有助于保留小字，但会增加耗时。服"
                                "务出错或请求超时后，文本识别自动使用原生 Tesseract。"
                                "公式请求上限 25 秒，失败时直接报错。"),
                 settingsPage);
  hint->setWordWrap(true);
  layout->addWidget(hint);
  layout->addStretch();
  connect(enabled, &QCheckBox::toggled, options, &QWidget::setEnabled);
  next(this, settingsPage, skeleton, QStringLiteral("SpectacleOCR"),
       QStringLiteral("edit-select-text"), QStringLiteral("文本识别设置"));
  options->setEnabled(enabled->isChecked());
  return result;
}

#include "formula_ui.h"
#include "settings.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <memory>

namespace {
std::atomic_bool formulaSelected{false};
QPointer<socr::FormulaUi> controller;
QPointer<QDialog> downloadDialog;

void attach(QQuickItem *item) {
  const auto children = item->childItems();
  for (auto *child : children)
    attach(child);
  if (item->property("socrAttached").toBool())
    return;
  auto *action = item->property("action").value<QObject *>();
  if (!action || !QString::fromLatin1(action->metaObject()->className())
                      .startsWith("OcrAction"))
    return;
  auto *engine = qmlEngine(item);
  if (!engine || !item->parentItem())
    return;
  QQmlComponent component(engine, QUrl("qrc:/socr/OcrButton.qml"));
  auto *context = new QQmlContext(qmlContext(item), item);
  context->setContextProperty("socrUi", controller.data());
  context->setContextProperty("socrOriginalButton", item);
  auto *object = component.create(context);
  if (!object) {
    qWarning() << "SpectacleOCR UI:" << component.errors();
    context->deleteLater();
    item->setProperty("socrAttached", true);
    return;
  }
  auto *button = qobject_cast<QQuickItem *>(object);
  if (!button) {
    delete object;
    delete context;
    return;
  }
  button->setParent(item->parentItem());
  button->setParentItem(item->parentItem());
  button->stackBefore(item);
  item->setProperty("socrAttached", true);
  qInfo() << "SpectacleOCR: text/formula button installed";
}

void initialize() {
  if (qEnvironmentVariableIsEmpty("SPECTACLE_OCR_SOCKET") ||
      qEnvironmentVariableIsEmpty("SPECTACLE_OCR_ROOT"))
    return;
  QTimer::singleShot(0, qApp, [] {
    controller = new socr::FormulaUi(qApp);
    auto *timer = new QTimer(controller);
    QObject::connect(timer, &QTimer::timeout, controller, [] {
      for (auto *window : QGuiApplication::allWindows()) {
        if (auto *quick = qobject_cast<QQuickWindow *>(window))
          attach(quick->contentItem());
      }
    });
    timer->start(400);
  });
}
Q_COREAPP_STARTUP_FUNCTION(initialize)
} // namespace

namespace socr {
bool formulaMode() { return formulaSelected.load(); }
void manageFormulaModels() {
  if (controller)
    controller->prepare(true);
}
void formulaError(const QString &message) {
  if (!qApp)
    return;
  QMetaObject::invokeMethod(
      qApp,
      [message] {
        if (controller)
          emit controller->failed(message);
      },
      Qt::QueuedConnection);
}
QString FormulaUi::root() { return qEnvironmentVariable("SPECTACLE_OCR_ROOT"); }
FormulaUi::FormulaUi(QObject *parent) : QObject(parent) {
  QSettings settings(configPath(), QSettings::IniFormat);
  formulaSelected.store(settings.value("OCR/DefaultMode", 0).toInt() == 1);
}
bool FormulaUi::formula() const { return formulaMode(); }
void FormulaUi::setFormula(bool value) {
  if (formulaSelected.exchange(value) != value)
    emit modeChanged();
}
bool FormulaUi::prepare(bool forceDownload) {
  if (!enabled() && !forceDownload) {
    emit failed(QStringLiteral("请先在 SpectacleOCR 设置页启用插件。"));
    return false;
  }
  bool ready = true;
  for (const auto &name :
       {"encoder_model.onnx", "decoder_model.onnx", "tokenizer.json"})
    ready &= QFileInfo(root() + "/assets/formula/" + name).size() > 0;
  if (ready && !forceDownload)
    return true;
  if (downloadDialog) {
    downloadDialog->raise();
    downloadDialog->activateWindow();
    return false;
  }
  auto *dialog = new QDialog;
  downloadDialog = dialog;
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(QStringLiteral("下载公式模型"));
  dialog->resize(440, 200);
  auto *layout = new QVBoxLayout(dialog);
  auto *label = new QLabel(
      QStringLiteral("公式识别需要下载约 120 MB 的 Pix2Text "
                     "模型。\n下载后可完全离线识别；普通文本识别不受影响。"),
      dialog);
  if (ready)
    label->setText(QStringLiteral(
        "公式模型已安装。将校验本地文件，缺失或损坏的文件会重新下载。"));
  label->setWordWrap(true);
  layout->addWidget(label);
  auto *progress = new QProgressBar(dialog);
  progress->setRange(0, 100);
  layout->addWidget(progress);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dialog);
  auto *download = buttons->addButton(QStringLiteral("下载模型"),
                                      QDialogButtonBox::ActionRole);
  if (ready)
    download->setText(QStringLiteral("校验模型"));
  layout->addWidget(buttons);
  auto *process = new QProcess(dialog);
  auto env = QProcessEnvironment::systemEnvironment();
  env.remove("LD_PRELOAD");
  env.remove("SPECTACLE_OCR_SOCKET");
  process->setProcessEnvironment(env);
  auto buffer = std::make_shared<QByteArray>();
  QObject::connect(process, &QProcess::readyReadStandardOutput, dialog, [=] {
    buffer->append(process->readAllStandardOutput());
    while (buffer->contains('\n')) {
      auto index = buffer->indexOf('\n');
      auto event = QJsonDocument::fromJson(buffer->left(index)).object();
      buffer->remove(0, index + 1);
      if (event["event"] == "progress") {
        double total = event["total"].toDouble(),
               received = event["received"].toDouble();
        progress->setRange(0, total > 0 ? 100 : 0);
        if (total > 0)
          progress->setValue(qMin(100, int(received * 100 / total)));
        label->setText(QStringLiteral("正在下载 %1\n%2 / %3 MB")
                           .arg(event["file"].toString())
                           .arg(received / 1e6, 0, 'f', 1)
                           .arg(total / 1e6, 0, 'f', 1));
      }
    }
  });
  QObject::connect(download, &QPushButton::clicked, dialog, [=] {
    download->setEnabled(false);
    label->setText(QStringLiteral("正在校验已有文件并连接下载服务器…"));
    progress->setRange(0, 0);
    process->start("python3", {root() + "/scripts/setup_assets.py", "--formula",
                               "--progress-json"});
  });
  QObject::connect(process, &QProcess::errorOccurred, dialog,
                   [=](QProcess::ProcessError error) {
                     if (error == QProcess::FailedToStart) {
                       label->setText(QStringLiteral("无法启动下载程序：") +
                                      process->errorString());
                       download->setEnabled(true);
                       progress->setRange(0, 100);
                     }
                   });
  QObject::connect(
      process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
      dialog, [=](int code, QProcess::ExitStatus status) {
        progress->setRange(0, 100);
        if (code == 0 && status == QProcess::NormalExit) {
          progress->setValue(100);
          label->setText(QStringLiteral(
              "模型已安装。关闭此窗口，点击“公式识别”即可使用。"));
          download->hide();
          buttons->setStandardButtons(QDialogButtonBox::Close);
        } else {
          label->setText(
              QStringLiteral("下载失败，可以重试。\n") +
              QString::fromUtf8(process->readAllStandardError()).right(600));
          download->setText(QStringLiteral("重试下载"));
          download->setEnabled(true);
        }
      });
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog,
                   &QDialog::reject);
  QObject::connect(dialog, &QDialog::finished, process, [=] {
    if (process->state() != QProcess::NotRunning) {
      process->terminate();
      // The downloader terminates curl and removes partial files on SIGTERM.
      if (!process->waitForFinished(4000)) {
        process->kill();
        process->waitForFinished(1000);
      }
    }
  });
  dialog->show();
  return false;
}
} // namespace socr

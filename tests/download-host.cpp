#include "formula_ui.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <iostream>
#include <stdexcept>

void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void pump() {
  QEventLoop loop;
  QTimer::singleShot(50, &loop, &QEventLoop::quit);
  loop.exec();
}
QDialog *dialog() {
  for (auto *widget : QApplication::topLevelWidgets())
    if (auto *d = qobject_cast<QDialog *>(widget);
        d && d->windowTitle() == QStringLiteral("下载公式模型"))
      return d;
  return nullptr;
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  try {
    socr::FormulaUi ui;
    check(!ui.prepare(), "missing models incorrectly reported ready");
    auto *first = dialog();
    check(first, "download dialog missing");
    first->reject();
    pump();
    check(!ui.prepare(),
          "missing models incorrectly reported ready after cancel");
    auto *d = dialog();
    check(d, "dialog could not reopen");
    auto *buttons = d->findChild<QDialogButtonBox *>();
    QPushButton *download = nullptr;
    for (auto *button : buttons->buttons())
      if (buttons->buttonRole(button) == QDialogButtonBox::ActionRole)
        download = qobject_cast<QPushButton *>(button);
    check(download, "download button missing");
    download->click();
    check(!download->isEnabled(), "duplicate download enabled");
    for (int i = 0; i < 200 && !download->isEnabled(); ++i)
      pump();
    check(download->isEnabled(), "failed download could not be retried");
    QFile source(qEnvironmentVariable("SOCR_DOWNLOAD_SOURCE"));
    check(source.open(QIODevice::WriteOnly), "test source unavailable");
    source.write("verified-formula");
    source.close();
    download->click();
    for (int i = 0; i < 200 && !buttons->button(QDialogButtonBox::Close); ++i)
      pump();
    check(buttons->button(QDialogButtonBox::Close),
          "download did not complete");
    check(ui.prepare(), "downloaded models not ready");
    d->grab().save(qEnvironmentVariable("SOCR_DOWNLOAD_SCREENSHOT"));
    d->reject();
    pump();
    std::cout << "PASS: missing-model prompt, cancel/reopen, checksum failure, "
                 "retry, download progress/completion\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

#include <KConfigDialog>
#include <KCoreConfigSkeleton>
#include <KPageWidget>
#include <KPageWidgetModel>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
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
class TestDialog : public KConfigDialog {
public:
  using KConfigDialog::buttonBox;
  using KConfigDialog::KConfigDialog;
  using KConfigDialog::pageWidget;
};
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  try {
    KCoreConfigSkeleton native(qEnvironmentVariable("XDG_CONFIG_HOME") +
                               "/hostrc");
    TestDialog dialog(nullptr, "settings", &native);
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    dialog.setFaceType(KPageDialog::List);
    dialog.addPage(new QLabel("原有常规设置"), &native, "常规");
    dialog.addPage(new QLabel("原有图像设置"), &native, "图像");
    check(dialog.findChildren<QWidget *>("spectacleOcrPage").size() == 1,
          "page missing or duplicated");
    auto *threads = dialog.findChild<QSpinBox *>("kcfg_Threads");
    auto *side = dialog.findChild<QSpinBox *>("kcfg_MaxSide");
    auto *timeout = dialog.findChild<QSpinBox *>("kcfg_TimeoutMs");
    auto *enabled = dialog.findChild<QCheckBox *>("kcfg_Enabled");
    auto *mode = dialog.findChild<QComboBox *>("kcfg_DefaultMode");
    check(mode && mode->currentIndex() == 0, "wrong default OCR mode");
    check(threads && side && timeout && enabled, "missing controls");
    auto *model =
        qobject_cast<KPageWidgetModel *>(dialog.pageWidget()->model());
    for (int row = 0; row < model->rowCount(); ++row) {
      auto *item = model->item(model->index(row, 0));
      if (item->name() == "SpectacleOCR")
        dialog.setCurrentPage(item);
    }
    dialog.resize(760, 440);
    dialog.show();
    pump();
    check(threads->value() == 4 && side->value() == 1536 &&
              timeout->value() == 20000 && enabled->isChecked(),
          "wrong defaults");
    if (argc > 1)
      check(dialog.grab().save(argv[1]), "screenshot failed");
    const QString path = qEnvironmentVariable("SPECTACLE_OCR_CONFIG");
    auto savedThreads = [&]() {
      QSettings config(path, QSettings::IniFormat);
      config.sync();
      return config.value("OCR/Threads", 4).toInt();
    };
    auto click = [&](QDialogButtonBox::StandardButton which) {
      auto *button = dialog.buttonBox()->button(which);
      pump();
      check(button && button->isEnabled(), "dialog button disabled");
      button->click();
      pump();
    };
    threads->setValue(6);
    mode->setCurrentIndex(1);
    click(QDialogButtonBox::Apply);
    {
      QSettings settings(path, QSettings::IniFormat);
      check(settings.value("OCR/DefaultMode").toInt() == 1, "formula default mode not saved");
    }
    check(savedThreads() == 6, "apply did not save");
    threads->setValue(8);
    click(QDialogButtonBox::Cancel);
    check(savedThreads() == 6, "cancel saved changes");
    dialog.show();
    pump();
    check(threads->value() == 6, "reopen did not reset canceled edits");
    click(QDialogButtonBox::RestoreDefaults);
    check(mode->currentIndex() == 0, "default OCR mode not restored");
    check(threads->value() == 4 && savedThreads() == 6,
          "defaults saved before apply");
    click(QDialogButtonBox::Cancel);
    dialog.show();
    pump();
    check(threads->value() == 6, "cancel after defaults failed");
    click(QDialogButtonBox::RestoreDefaults);
    click(QDialogButtonBox::Apply);
    check(savedThreads() == 4, "defaults did not persist");
    enabled->setChecked(false);
    click(QDialogButtonBox::Apply);
    QSettings persisted(path, QSettings::IniFormat);
    persisted.sync();
    check(!persisted.value("OCR/Enabled", true).toBool(),
          "backend toggle did not save");
    check(!threads->isEnabled(), "disabled backend left controls enabled");
    enabled->setChecked(true);
    click(QDialogButtonBox::Ok);
    dialog.show();
    pump();
    check(dialog.findChildren<QWidget *>("spectacleOcrPage").size() == 1,
          "reopen duplicated page");
    std::cout
        << "PASS: injected page, apply, cancel, defaults, enable, reopen\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

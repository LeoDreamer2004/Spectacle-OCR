// Runs inside an offscreen Spectacle instance, using only a supplied fixture.
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>

namespace {
QQuickItem *find(QQuickItem *item, const QString &name) {
  if (item->objectName() == name)
    return item;
  for (auto *child : item->childItems())
    if (auto *result = find(child, name))
      return result;
  return nullptr;
}
void start() {
  QTimer::singleShot(0, qApp, [] {
    auto *timer = new QTimer(qApp);
    QObject::connect(
        timer, &QTimer::timeout, qApp,
        [timer, ticks = 0, stage = 0,
         observer = static_cast<QObject *>(nullptr)]() mutable {
          if (++ticks > 160) {
            std::fprintf(stderr, "UI probe timeout at stage %d\n", stage);
            qApp->exit(2);
            return;
          }
          for (auto *window : QGuiApplication::allWindows()) {
            auto *quick = qobject_cast<QQuickWindow *>(window);
            if (!quick)
              continue;
            auto *root = find(quick->contentItem(), "spectacleOcrButton");
            if (!root)
              continue;
            auto *button = find(root, "socrRecognize");
            auto *menu = find(root, "socrModeMenu");
            if (!button || !menu) {
              qApp->exit(3);
              return;
            }
            auto *context = qmlContext(root);
            auto *ui = context->contextProperty("socrUi").value<QObject *>();
            if (stage == 0) {
              if (!button->isEnabled())
                continue;
              QMetaObject::invokeMethod(menu, "clicked");
              auto *choice = root->findChild<QObject *>("socrFormulaMode");
              auto *popup = root->findChild<QObject *>("socrModes");
              if (!choice || !popup) {
                qApp->exit(9);
                return;
              }
              QMetaObject::invokeMethod(choice, "triggered");
              QMetaObject::invokeMethod(popup, "close");
              QQmlComponent component(qmlEngine(root));
              component.setData(
                  R"(import QtQuick; import org.kde.spectacle.private; QtObject {
            id: monitor
            readonly property int status: SpectacleCore.ocrStatus
            property string failure: ""
            property Connections handler: Connections {
              target: socrUi
              function onFailed(message) { monitor.failure = message }
            }
          })",
                  QUrl());
              observer = component.create(context);
              if (!observer) {
                qWarning() << component.errors();
                qApp->exit(4);
                return;
              }
              observer->setParent(timer);
              stage = 1;
              return;
            }
            if (stage == 1) {
              quick->grabWindow().save(
                  qEnvironmentVariable("SOCR_UI_SCREENSHOT"));
              if (!button->property("text").toString().contains(
                      QStringLiteral("公式"))) {
                qApp->exit(5);
                return;
              }
              QMetaObject::invokeMethod(button, "clicked");
              stage = 2;
              return;
            }
            if (stage == 2) {
              if (observer->property("status").toInt() == 1) {
                if (button->isEnabled() || menu->isEnabled()) {
                  qApp->exit(6);
                  return;
                }
                return;
              }
              const auto text = qApp->clipboard()->text();
              if (qEnvironmentVariableIsSet("SOCR_UI_EXPECT_ERROR")) {
                if (observer->property("failure").toString().isEmpty()) {
                  std::fprintf(stderr, "Formula failure was not displayed\n");
                  qApp->exit(10);
                  return;
                }
                if (!text.isEmpty()) {
                  qApp->exit(11);
                  return;
                }
                std::puts("PASS: formula service failure is displayed without "
                          "Tesseract fallback");
                qApp->quit();
                return;
              }
              if (!text.contains("\\frac")) {
                std::fprintf(stderr, "Unexpected LaTeX: %s\n",
                             text.toUtf8().constData());
                qApp->exit(7);
                return;
              }
              ui->setProperty("formula", false);
              stage = 3;
              return;
            }
            if (stage == 3) {
              if (!button->property("text").toString().contains(
                      QStringLiteral("文本")) ||
                  !button->isEnabled()) {
                qApp->exit(8);
                return;
              }
              std::puts("PASS: real Spectacle button injection, mode switch, "
                        "formula output, busy state, switch back");
              timer->stop();
              qApp->quit();
              return;
            }
          }
        });
    timer->start(200);
  });
}
Q_COREAPP_STARTUP_FUNCTION(start)
} // namespace

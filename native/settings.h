#pragma once
#include <QSettings>
#include <QStandardPaths>
#include <QString>

namespace socr {
inline QString configPath() {
  const auto override = qEnvironmentVariable("SPECTACLE_OCR_CONFIG");
  return override.isEmpty() ? QStandardPaths::writableLocation(
                                  QStandardPaths::GenericConfigLocation) +
                                  "/spectacle-ocrrc"
                            : override;
}
inline bool enabled() {
  QSettings config(configPath(), QSettings::IniFormat);
  config.sync();
  return config.value("OCR/Enabled", true).toBool();
}
inline int timeout() {
  QSettings config(configPath(), QSettings::IniFormat);
  config.sync();
  bool ok = false;
  const int value = config.value("OCR/TimeoutMs", 20000).toInt(&ok);
  return ok && value >= 100 && value <= 25000 ? value : 20000;
}
} // namespace socr

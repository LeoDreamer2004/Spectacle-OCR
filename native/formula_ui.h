#pragma once
#include <QObject>
#include <QString>

namespace socr {
bool formulaMode();
void formulaError(const QString &message);
void manageFormulaModels();
class FormulaUi final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool formula READ formula WRITE setFormula NOTIFY modeChanged)
public:
  explicit FormulaUi(QObject *parent = nullptr);
  bool formula() const;
  void setFormula(bool formula);
  Q_INVOKABLE bool prepare(bool forceDownload = false);
  static QString root();
signals:
  void modeChanged();
  void failed(const QString &message);
};
} // namespace socr

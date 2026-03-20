#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>

class UpdateChecker : public QObject {
  Q_OBJECT

public:
  explicit UpdateChecker(QObject *parent = nullptr);
  void check();

  static QString currentVersion() { return "1.2.1"; }

signals:
  void updateAvailable(const QString &latestVersion, const QString &message);
  void upToDate();

private:
  QNetworkAccessManager *nam;
};

#endif
#include "updatechecker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent), nam(new QNetworkAccessManager(this)) {}

void UpdateChecker::check() {
  QNetworkRequest request{QUrl("https://fursense.lol/version.json")};
  request.setHeader(QNetworkRequest::UserAgentHeader, "CS2Vault/1.2.1");
  QNetworkReply *reply = nam->get(request);

  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
      return; // fail silently, update check is non-critical

    QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    QString latest = obj["latest"].toString();
    QString message = obj["message"].toString();

    if (latest.isEmpty() || latest == currentVersion()) {
      emit upToDate();
      return;
    }

    emit updateAvailable(latest, message);
  });
}
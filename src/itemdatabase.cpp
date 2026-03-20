#include "itemdatabase.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>

ItemDatabase::ItemDatabase(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)) {}

void ItemDatabase::load() {
  if (m_loaded)
    return;

  qInfo() << "Fetching item database from" << URL;

  QNetworkRequest request{QUrl(URL)};
  request.setHeader(QNetworkRequest::UserAgentHeader, "CS2Vault/1.0.0");
  QNetworkReply *reply = m_nam->get(request);

  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();

    int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200) {
      emit error(QString("Failed to fetch item database: HTTP %1").arg(status));
      return;
    }

    parse(reply->readAll());
  });
}

void ItemDatabase::reload() {
  m_loaded = false;
  load();
}

void ItemDatabase::parse(const QByteArray &data) {
  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject()) {
    emit error("Invalid item database JSON");
    return;
  }

  QJsonObject items = doc.object()["items"].toObject();
  m_items.clear();

  for (auto it = items.constBegin(); it != items.constEnd(); ++it) {
    QJsonObject entry = it.value().toObject();
    ItemInfo info;

    if (entry.contains("i"))
      info.iconUrl = entry["i"].toString();
    if (entry.contains("c"))
      info.rarityColor = QColor(entry["c"].toString());
    if (entry.contains("mn"))
      info.minFloat = (float)entry["mn"].toDouble();
    if (entry.contains("mx"))
      info.maxFloat = (float)entry["mx"].toDouble();
    if (entry.contains("st"))
      info.stattrak = entry["st"].toInt() == 1;

    m_items[it.key()] = info;
  }

  m_loaded = true;
  qInfo() << "Item database loaded —" << m_items.size() << "items";
  emit loaded();
}

QString ItemDatabase::stripCondition(const QString &name) {
  static const QRegularExpression rx(
      R"(\s*\((Factory New|Minimal Wear|Field-Tested|Well-Worn|Battle-Scarred)\)$)");
  return QString(name).remove(rx);
}

ItemInfo ItemDatabase::lookup(const QString &marketHashName) const {
  if (m_items.contains(marketHashName))
    return m_items[marketHashName];
  return m_items.value(stripCondition(marketHashName), ItemInfo{});
}

bool ItemDatabase::hasItem(const QString &marketHashName) const {
  return m_items.contains(marketHashName) ||
         m_items.contains(stripCondition(marketHashName));
}
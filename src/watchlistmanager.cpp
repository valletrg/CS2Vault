#include "watchlistmanager.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

WatchlistManager::WatchlistManager(QObject *parent) : QObject(parent) {
  QString dataPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dataPath);
  dataFile = dataPath + "/watchlists.json";

  loadFromFile();
}

WatchlistManager::~WatchlistManager() { saveToFile(); }

QString WatchlistManager::generateId() const {
  return QUuid::createUuid().toString(QUuid::Id128);
}

QString WatchlistManager::getCurrentTimestamp() const {
  return QDateTime::currentDateTime().toString(Qt::ISODate);
}

QString WatchlistManager::createWatchlist(const QString &name,
                                          const QString &description) {
  Watchlist wl;
  wl.id = generateId();
  wl.name = name;
  wl.description = description;
  wl.createdAt = getCurrentTimestamp();
  wl.updatedAt = wl.createdAt;

  watchlists[wl.id] = wl;
  saveToFile();

  emit watchlistCreated(wl.id, wl.name);
  return wl.id;
}

bool WatchlistManager::deleteWatchlist(const QString &id) {
  if (!watchlists.contains(id))
    return false;

  watchlists.remove(id);
  saveToFile();

  emit watchlistDeleted(id);
  return true;
}

bool WatchlistManager::renameWatchlist(const QString &id,
                                       const QString &newName) {
  if (!watchlists.contains(id))
    return false;

  watchlists[id].name = newName;
  watchlists[id].updatedAt = getCurrentTimestamp();
  saveToFile();

  emit watchlistRenamed(id, newName);
  return true;
}

QVector<Watchlist> WatchlistManager::getAllWatchlists() const {
  return watchlists.values().toVector();
}

Watchlist WatchlistManager::getWatchlist(const QString &id) const {
  return watchlists.value(id);
}

bool WatchlistManager::watchlistExists(const QString &id) const {
  return watchlists.contains(id);
}

void WatchlistManager::addItem(const QString &watchlistId,
                               const WatchlistItem &item) {
  if (!watchlists.contains(watchlistId))
    return;

  watchlists[watchlistId].items.append(item);
  watchlists[watchlistId].updatedAt = getCurrentTimestamp();
  saveToFile();

  emit itemAdded(watchlistId, item);
  emit watchlistChanged(watchlistId);
}

bool WatchlistManager::removeItem(const QString &watchlistId, int index) {
  if (!watchlists.contains(watchlistId))
    return false;

  Watchlist &wl = watchlists[watchlistId];
  if (index < 0 || index >= wl.items.size())
    return false;

  wl.items.remove(index);
  wl.updatedAt = getCurrentTimestamp();
  saveToFile();

  emit itemRemoved(watchlistId, index);
  emit watchlistChanged(watchlistId);
  return true;
}

void WatchlistManager::updateItemPrice(const QString &watchlistId, int index,
                                       double price) {
  if (!watchlists.contains(watchlistId))
    return;

  Watchlist &wl = watchlists[watchlistId];
  if (index < 0 || index >= wl.items.size())
    return;

  wl.items[index].currentPrice = price;
  wl.updatedAt = getCurrentTimestamp();
}

void WatchlistManager::recordPriceHistory(const QString &watchlistId) {
  if (!watchlists.contains(watchlistId))
    return;

  Watchlist &wl = watchlists[watchlistId];
  qint64 now = QDateTime::currentSecsSinceEpoch();
  QDate today = QDate::currentDate();
  bool changed = false;

  for (WatchlistItem &item : wl.items) {
    if (item.currentPrice <= 0.0)
      continue;

    bool shouldRecord = false;
    if (item.priceHistory.isEmpty()) {
      shouldRecord = true;
    } else {
      QDate lastDate =
          QDateTime::fromSecsSinceEpoch(item.priceHistory.last().timestamp)
              .date();
      shouldRecord = (lastDate < today);
    }

    if (shouldRecord) {
      WatchlistPricePoint pt;
      pt.timestamp = now;
      pt.price = item.currentPrice;
      item.priceHistory.append(pt);
      changed = true;
    }
  }

  if (changed) {
    wl.updatedAt = getCurrentTimestamp();
    saveToFile();
    emit watchlistChanged(watchlistId);
  }
}

void WatchlistManager::saveToFile() {
  QJsonObject root;
  QJsonArray watchlistsArray;

  for (const Watchlist &wl : watchlists) {
    QJsonObject wlObj;
    wlObj["id"] = wl.id;
    wlObj["name"] = wl.name;
    wlObj["description"] = wl.description;
    wlObj["createdAt"] = wl.createdAt;
    wlObj["updatedAt"] = wl.updatedAt;

    QJsonArray itemsArray;
    for (const WatchlistItem &item : wl.items) {
      itemsArray.append(itemToJson(item));
    }
    wlObj["items"] = itemsArray;

    watchlistsArray.append(wlObj);
  }

  root["watchlists"] = watchlistsArray;
  root["version"] = "1.0";

  QFile file(dataFile);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
  }
}

void WatchlistManager::loadFromFile() {
  QFile file(dataFile);
  if (!file.exists()) {
    createWatchlist("My Watchlist", "Default watchlist");
    return;
  }

  if (!file.open(QIODevice::ReadOnly))
    return;

  QByteArray data = file.readAll();
  file.close();

  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject())
    return;

  QJsonObject root = doc.object();
  QJsonArray watchlistsArray = root["watchlists"].toArray();

  watchlists.clear();

  for (const QJsonValue &wlVal : watchlistsArray) {
    QJsonObject wlObj = wlVal.toObject();

    Watchlist wl;
    wl.id = wlObj["id"].toString();
    wl.name = wlObj["name"].toString();
    wl.description = wlObj["description"].toString();
    wl.createdAt = wlObj["createdAt"].toString();
    wl.updatedAt = wlObj["updatedAt"].toString();

    QJsonArray itemsArray = wlObj["items"].toArray();
    for (const QJsonValue &itemVal : itemsArray) {
      wl.items.append(itemFromJson(itemVal.toObject()));
    }

    watchlists[wl.id] = wl;
  }

  if (watchlists.isEmpty()) {
    createWatchlist("My Watchlist", "Default watchlist");
  }
}

WatchlistItem
WatchlistManager::itemFromJson(const QJsonObject &obj) const {
  WatchlistItem item;
  item.skinName = obj["skinName"].toString();
  item.condition = obj["condition"].toString();
  item.currentPrice = obj["currentPrice"].toDouble();
  item.addedDate = obj["addedDate"].toString();
  item.notes = obj["notes"].toString();

  QJsonArray historyArray = obj["priceHistory"].toArray();
  for (const QJsonValue &ptVal : historyArray) {
    QJsonObject ptObj = ptVal.toObject();
    WatchlistPricePoint pt;
    pt.timestamp = ptObj["timestamp"].toVariant().toLongLong();
    pt.price = ptObj["price"].toDouble();
    item.priceHistory.append(pt);
  }

  return item;
}

QJsonObject
WatchlistManager::itemToJson(const WatchlistItem &item) const {
  QJsonObject obj;
  obj["skinName"] = item.skinName;
  obj["condition"] = item.condition;
  obj["currentPrice"] = item.currentPrice;
  obj["addedDate"] = item.addedDate;
  obj["notes"] = item.notes;

  QJsonArray historyArray;
  for (const WatchlistPricePoint &pt : item.priceHistory) {
    QJsonObject ptObj;
    ptObj["timestamp"] = pt.timestamp;
    ptObj["price"] = pt.price;
    historyArray.append(ptObj);
  }
  obj["priceHistory"] = historyArray;

  return obj;
}

#include "tradehistory.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

TradeHistoryManager::TradeHistoryManager(QObject *parent) : QObject(parent) {
  QString dataPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dataPath);
  m_dataFile = dataPath + "/trade_history.json";
  loadFromFile();
}

TradeHistoryManager::~TradeHistoryManager() { saveToFile(); }

void TradeHistoryManager::addEntry(const TradeHistoryEntry &entry) {
  TradeHistoryEntry e = entry;
  if (e.id.isEmpty())
    e.id = QUuid::createUuid().toString(QUuid::Id128);
  m_entries.append(e);
  saveToFile();
  emit historyChanged();
}

bool TradeHistoryManager::removeEntry(const QString &id) {
  for (int i = 0; i < m_entries.size(); ++i) {
    if (m_entries[i].id == id) {
      m_entries.remove(i);
      saveToFile();
      emit historyChanged();
      return true;
    }
  }
  return false;
}

bool TradeHistoryManager::updateEntry(const TradeHistoryEntry &entry) {
  for (int i = 0; i < m_entries.size(); ++i) {
    if (m_entries[i].id == entry.id) {
      m_entries[i] = entry;
      saveToFile();
      emit historyChanged();
      return true;
    }
  }
  return false;
}

QVector<TradeHistoryEntry> TradeHistoryManager::entries() const {
  return m_entries;
}

double TradeHistoryManager::totalSpent() const {
  double total = 0.0;
  for (const auto &e : m_entries) {
    if (e.type == "manual_buy")
      total += e.price * e.quantity;
    else if (e.buyPrice > 0.0)
      total += e.buyPrice * e.quantity;
  }
  return total;
}

double TradeHistoryManager::totalReceived() const {
  double total = 0.0;
  for (const auto &e : m_entries) {
    if (e.type == "manual_sell")
      total += e.price * e.quantity;
    else if (e.sellPrice > 0.0)
      total += e.sellPrice * e.quantity;
  }
  return total;
}

double TradeHistoryManager::netProfit() const {
  return totalReceived() - totalSpent();
}

void TradeHistoryManager::saveToFile() {
  QJsonArray arr;
  for (const auto &e : m_entries)
    arr.append(entryToJson(e));

  QJsonObject root;
  root["entries"] = arr;
  root["version"] = 1;

  QFile file(m_dataFile);
  if (file.open(QIODevice::WriteOnly))
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void TradeHistoryManager::loadFromFile() {
  QFile file(m_dataFile);
  if (!file.exists() || !file.open(QIODevice::ReadOnly))
    return;

  QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if (!doc.isObject())
    return;

  QJsonArray arr = doc.object()["entries"].toArray();
  m_entries.clear();
  for (const QJsonValue &v : arr)
    m_entries.append(entryFromJson(v.toObject()));
}

TradeHistoryEntry
TradeHistoryManager::entryFromJson(const QJsonObject &obj) const {
  TradeHistoryEntry e;
  e.id = obj["id"].toString();
  e.itemName = obj["itemName"].toString();
  e.type = obj["type"].toString();
  e.price = obj["price"].toDouble();
  e.buyPrice = obj["buyPrice"].toDouble();
  e.sellPrice = obj["sellPrice"].toDouble();
  e.quantity = obj["quantity"].toInt(1);
  e.timestamp = obj["timestamp"].toVariant().toLongLong();
  e.notes = obj["notes"].toString();
  e.storageUnit = obj["storageUnit"].toString();

  // Migrate old "buy"/"sell" types to "manual_buy"/"manual_sell"
  if (e.type == "buy")
    e.type = "manual_buy";
  else if (e.type == "sell")
    e.type = "manual_sell";

  return e;
}

QJsonObject
TradeHistoryManager::entryToJson(const TradeHistoryEntry &entry) const {
  QJsonObject obj;
  obj["id"] = entry.id;
  obj["itemName"] = entry.itemName;
  obj["type"] = entry.type;
  obj["price"] = entry.price;
  obj["buyPrice"] = entry.buyPrice;
  obj["sellPrice"] = entry.sellPrice;
  obj["quantity"] = entry.quantity;
  obj["timestamp"] = entry.timestamp;
  obj["notes"] = entry.notes;
  obj["storageUnit"] = entry.storageUnit;
  return obj;
}

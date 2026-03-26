#include "portfoliomanager.h"

#include <QDateTime>
#include <QTimeZone>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

PortfolioManager::PortfolioManager(QObject *parent) : QObject(parent) {
  QString dataPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dataPath);
  dataFile = dataPath + "/portfolios.json";

  loadFromFile();
}

PortfolioManager::~PortfolioManager() { saveToFile(); }

QString PortfolioManager::generateId() const {
  return QUuid::createUuid().toString(QUuid::Id128);
}

QString PortfolioManager::getCurrentTimestamp() const {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString PortfolioManager::createPortfolio(const QString &name,
                                          const QString &description) {
  Portfolio portfolio;
  portfolio.id = generateId();
  portfolio.name = name;
  portfolio.description = description;
  portfolio.createdAt = getCurrentTimestamp();
  portfolio.updatedAt = portfolio.createdAt;

  portfolios[portfolio.id] = portfolio;
  saveToFile();

  emit portfolioCreated(portfolio.id, portfolio.name);
  return portfolio.id;
}

bool PortfolioManager::deletePortfolio(const QString &id) {
  if (!portfolios.contains(id)) {
    return false;
  }

  portfolios.remove(id);
  saveToFile();

  emit portfolioDeleted(id);
  return true;
}

bool PortfolioManager::renamePortfolio(const QString &id,
                                       const QString &newName) {
  if (!portfolios.contains(id)) {
    return false;
  }

  portfolios[id].name = newName;
  portfolios[id].updatedAt = getCurrentTimestamp();
  saveToFile();

  emit portfolioRenamed(id, newName);
  return true;
}

QVector<Portfolio> PortfolioManager::getAllPortfolios() const {
  return portfolios.values().toVector();
}

Portfolio PortfolioManager::getPortfolio(const QString &id) const {
  return portfolios.value(id);
}

bool PortfolioManager::portfolioExists(const QString &id) const {
  return portfolios.contains(id);
}

void PortfolioManager::addItem(const QString &portfolioId,
                               const PortfolioItem &item) {
  if (!portfolios.contains(portfolioId)) {
    return;
  }

  portfolios[portfolioId].items.append(item);
  portfolios[portfolioId].updatedAt = getCurrentTimestamp();
  saveToFile();

  emit itemAdded(portfolioId, item);
  emit portfolioChanged(portfolioId);
}

bool PortfolioManager::removeItem(const QString &portfolioId, int index) {
  if (!portfolios.contains(portfolioId)) {
    return false;
  }

  Portfolio &portfolio = portfolios[portfolioId];
  if (index < 0 || index >= portfolio.items.size()) {
    return false;
  }

  portfolio.items.remove(index);
  portfolio.updatedAt = getCurrentTimestamp();
  saveToFile();

  emit itemRemoved(portfolioId, index);
  emit portfolioChanged(portfolioId);
  return true;
}

void PortfolioManager::updateItem(const QString &portfolioId, int index,
                                  const PortfolioItem &item) {
  if (!portfolios.contains(portfolioId)) {
    return;
  }

  Portfolio &portfolio = portfolios[portfolioId];
  if (index < 0 || index >= portfolio.items.size()) {
    return;
  }

  portfolio.items[index] = item;
  portfolio.updatedAt = getCurrentTimestamp();
  saveToFile();

  emit portfolioChanged(portfolioId);
}

void PortfolioManager::clearPortfolio(const QString &portfolioId) {
  if (!portfolios.contains(portfolioId)) {
    return;
  }

  portfolios[portfolioId].items.clear();
  portfolios[portfolioId].updatedAt = getCurrentTimestamp();
  saveToFile();

  emit portfolioChanged(portfolioId);
}

void PortfolioManager::importFromSteamInventory(
    const QString &portfolioId, const QVector<PortfolioItem> &items) {
  if (!portfolios.contains(portfolioId)) {
    return;
  }

  for (const PortfolioItem &item : items) {
    portfolios[portfolioId].items.append(item);
  }

  portfolios[portfolioId].updatedAt = getCurrentTimestamp();
  saveToFile();

  emit portfolioChanged(portfolioId);
}

void PortfolioManager::recordHistoryPoint(const QString &portfolioId,
                                          bool forced) {
  if (!portfolios.contains(portfolioId)) {
    return;
  }

  Portfolio &portfolio = portfolios[portfolioId];

  double totalCost = 0.0;
  double totalValue = 0.0;

  for (const PortfolioItem &item : portfolio.items) {
    totalCost += (item.buyPrice * item.quantity);
    totalValue += (item.currentPrice * item.quantity);
  }

  qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();

  bool shouldRecord = forced;
  if (!shouldRecord) {
    if (portfolio.history.isEmpty()) {
      shouldRecord = true;
    } else {
      const PortfolioHistoryPoint &lastPoint = portfolio.history.last();
      // Only record once per calendar day — keeps stored data very lean
      QDate lastDate =
          QDateTime::fromMSecsSinceEpoch(lastPoint.timestamp, QTimeZone::UTC).date();
      QDate today = QDate::currentDate();
      shouldRecord = (lastDate < today);
    }
  }

  if (shouldRecord) {
    PortfolioHistoryPoint pt;
    pt.timestamp = currentTimestamp;
    pt.totalCost = totalCost;
    pt.totalValue = totalValue;

    portfolio.history.append(pt);
    portfolio.updatedAt = getCurrentTimestamp();
    saveToFile();

    emit portfolioChanged(portfolioId);
  }
}

void PortfolioManager::saveToFile() {
  QJsonObject root;
  QJsonArray portfoliosArray;

  for (const Portfolio &portfolio : portfolios) {
    QJsonObject portfolioObj;
    portfolioObj["id"] = portfolio.id;
    portfolioObj["name"] = portfolio.name;
    portfolioObj["description"] = portfolio.description;
    portfolioObj["createdAt"] = portfolio.createdAt;
    portfolioObj["updatedAt"] = portfolio.updatedAt;

    QJsonArray itemsArray;
    for (const PortfolioItem &item : portfolio.items) {
      itemsArray.append(itemToJson(item));
    }
    portfolioObj["items"] = itemsArray;

    QJsonArray historyArray;
    for (const PortfolioHistoryPoint &pt : portfolio.history) {
      QJsonObject ptObj;
      ptObj["timestamp"] = pt.timestamp;
      ptObj["totalCost"] = pt.totalCost;
      ptObj["totalValue"] = pt.totalValue;
      historyArray.append(ptObj);
    }
    portfolioObj["history"] = historyArray;

    portfoliosArray.append(portfolioObj);
  }

  root["portfolios"] = portfoliosArray;
  root["version"] = 2;

  QFile file(dataFile);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
  }
}

void PortfolioManager::loadFromFile() {
  QFile file(dataFile);
  if (!file.exists()) {
    // Create default portfolio
    createPortfolio("My Inventory", "Default portfolio for CS2 skins");
    return;
  }

  if (!file.open(QIODevice::ReadOnly)) {
    return;
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject()) {
    return;
  }

  QJsonObject root = doc.object();

  // Version 2 introduced millisecond UTC timestamps.
  // Any file without version 2 has second-based timestamps that would
  // corrupt the chart axis, so wipe history and let it rebuild cleanly.
  bool historyValid = (root["version"].toInt(0) >= 2);

  QJsonArray portfoliosArray = root["portfolios"].toArray();

  portfolios.clear();

  for (const QJsonValue &portfolioVal : portfoliosArray) {
    QJsonObject portfolioObj = portfolioVal.toObject();

    Portfolio portfolio;
    portfolio.id = portfolioObj["id"].toString();
    portfolio.name = portfolioObj["name"].toString();
    portfolio.description = portfolioObj["description"].toString();
    portfolio.createdAt = portfolioObj["createdAt"].toString();
    portfolio.updatedAt = portfolioObj["updatedAt"].toString();

    QJsonArray itemsArray = portfolioObj["items"].toArray();
    for (const QJsonValue &itemVal : itemsArray) {
      portfolio.items.append(itemFromJson(itemVal.toObject()));
    }

    if (historyValid) {
      QJsonArray historyArray = portfolioObj["history"].toArray();
      for (const QJsonValue &ptVal : historyArray) {
        QJsonObject ptObj = ptVal.toObject();
        PortfolioHistoryPoint pt;
        pt.timestamp = ptObj["timestamp"].toVariant().toLongLong();
        pt.totalCost = ptObj["totalCost"].toDouble();
        pt.totalValue = ptObj["totalValue"].toDouble();
        portfolio.history.append(pt);
      }
    }

    portfolios[portfolio.id] = portfolio;
  }

  // Create default portfolio if none exist
  if (portfolios.isEmpty()) {
    createPortfolio("My Inventory", "Default portfolio for CS2 skins");
  }
}

PortfolioItem PortfolioManager::itemFromJson(const QJsonObject &obj) const {
  PortfolioItem item;
  item.skinName = obj["skinName"].toString();
  item.condition = obj["condition"].toString();
  item.floatValue = obj["floatValue"].toDouble();
  item.quantity = obj["quantity"].toInt(1);
  item.buyPrice = obj["buyPrice"].toDouble();
  item.currentPrice = obj["currentPrice"].toDouble();
  item.purchaseDate = obj["purchaseDate"].toString();
  item.notes = obj["notes"].toString();
  item.iconUrl = obj["iconUrl"].toString();
  item.rarity = obj["rarity"].toString();
  return item;
}

QJsonObject PortfolioManager::itemToJson(const PortfolioItem &item) const {
  QJsonObject obj;
  obj["skinName"] = item.skinName;
  obj["condition"] = item.condition;
  obj["floatValue"] = item.floatValue;
  obj["quantity"] = item.quantity;
  obj["buyPrice"] = item.buyPrice;
  obj["currentPrice"] = item.currentPrice;
  obj["purchaseDate"] = item.purchaseDate;
  obj["notes"] = item.notes;
  obj["iconUrl"] = item.iconUrl;
  obj["rarity"] = item.rarity;
  return obj;
}

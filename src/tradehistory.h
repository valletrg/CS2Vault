#ifndef TRADEHISTORY_H
#define TRADEHISTORY_H

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVector>

struct TradeHistoryEntry {
  QString id;
  QString itemName;
  // "manual_buy", "manual_sell", "acquired", "traded_away",
  // "storage_in", "storage_out"
  QString type;
  double price = 0.0;
  double buyPrice = 0.0;
  double sellPrice = 0.0;
  int quantity = 1;
  qint64 timestamp = 0; // milliseconds UTC
  QString notes;
  QString storageUnit;
};

class TradeHistoryManager : public QObject {
  Q_OBJECT

public:
  explicit TradeHistoryManager(QObject *parent = nullptr);
  ~TradeHistoryManager();

  void addEntry(const TradeHistoryEntry &entry);
  bool removeEntry(const QString &id);
  bool updateEntry(const TradeHistoryEntry &entry);
  QVector<TradeHistoryEntry> entries() const;

  double totalSpent() const;
  double totalReceived() const;
  double netProfit() const;

  void saveToFile();
  void loadFromFile();

signals:
  void historyChanged();

private:
  TradeHistoryEntry entryFromJson(const QJsonObject &obj) const;
  QJsonObject entryToJson(const TradeHistoryEntry &entry) const;

  QVector<TradeHistoryEntry> m_entries;
  QString m_dataFile;
};

#endif // TRADEHISTORY_H

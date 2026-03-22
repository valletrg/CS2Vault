#ifndef WATCHLISTMANAGER_H
#define WATCHLISTMANAGER_H

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVector>

struct WatchlistPricePoint {
  qint64 timestamp;
  double price;
};

struct WatchlistItem {
  QString skinName;
  QString condition;
  double currentPrice = 0.0;
  QString addedDate;
  QString notes;
  QVector<WatchlistPricePoint> priceHistory;
};

struct Watchlist {
  QString id;
  QString name;
  QString description;
  QVector<WatchlistItem> items;
  QString createdAt;
  QString updatedAt;
};

class WatchlistManager : public QObject {
  Q_OBJECT

public:
  explicit WatchlistManager(QObject *parent = nullptr);
  ~WatchlistManager();

  QString createWatchlist(const QString &name,
                          const QString &description = "");
  bool deleteWatchlist(const QString &id);
  bool renameWatchlist(const QString &id, const QString &newName);
  QVector<Watchlist> getAllWatchlists() const;
  Watchlist getWatchlist(const QString &id) const;
  bool watchlistExists(const QString &id) const;

  void addItem(const QString &watchlistId, const WatchlistItem &item);
  bool removeItem(const QString &watchlistId, int index);
  void updateItemPrice(const QString &watchlistId, int index, double price);
  void recordPriceHistory(const QString &watchlistId);

  void saveToFile();
  void loadFromFile();

signals:
  void watchlistCreated(const QString &id, const QString &name);
  void watchlistDeleted(const QString &id);
  void watchlistRenamed(const QString &id, const QString &newName);
  void itemAdded(const QString &watchlistId, const WatchlistItem &item);
  void itemRemoved(const QString &watchlistId, int index);
  void watchlistChanged(const QString &watchlistId);

private:
  QString generateId() const;
  QString getCurrentTimestamp() const;
  WatchlistItem itemFromJson(const QJsonObject &obj) const;
  QJsonObject itemToJson(const WatchlistItem &item) const;

  QMap<QString, Watchlist> watchlists;
  QString dataFile;
};

#endif // WATCHLISTMANAGER_H

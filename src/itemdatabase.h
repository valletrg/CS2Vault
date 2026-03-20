#ifndef ITEMDATABASE_H
#define ITEMDATABASE_H

#include <QColor>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

struct ItemInfo {
  QColor rarityColor;
  float minFloat = 0.0f;
  float maxFloat = 1.0f;
  bool stattrak = false;
  QString iconUrl;
};

class ItemDatabase : public QObject {
  Q_OBJECT

public:
  explicit ItemDatabase(QObject *parent = nullptr);

  void load();
  void reload();
  bool isLoaded() const { return m_loaded; }
  int itemCount() const { return m_items.size(); }

  // Accepts full market_hash_name with or without condition
  // e.g. "AK-47 | Redline (Field-Tested)" or "AK-47 | Redline"
  ItemInfo lookup(const QString &marketHashName) const;
  bool hasItem(const QString &marketHashName) const;

signals:
  void loaded();
  void error(const QString &message);

private:
  void parse(const QByteArray &data);
  static QString stripCondition(const QString &name);

  QNetworkAccessManager *m_nam;
  QMap<QString, ItemInfo> m_items;
  bool m_loaded = false;

  static constexpr const char *URL = "https://fursense.lol/items.json";
};

#endif // ITEMDATABASE_H
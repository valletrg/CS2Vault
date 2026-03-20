#ifndef PRICEEMPIREAPI_H
#define PRICEEMPIREAPI_H

#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

class PriceEmpireAPI : public QObject {
  Q_OBJECT

public:
  explicit PriceEmpireAPI(QObject *parent = nullptr);
  ~PriceEmpireAPI();

  bool isValid() const;
  bool testConnection();
  void setSourceUrl(const QString &url);
  void initSourceUrl(const QString &url) { m_sourceUrl = url; }

  // Instant map lookup — returns 0.0 if not found or prices not loaded yet
  double fetchPrice(const QString &skinName, const QString &currency = "USD",
                    int retryCount = 0);

  // Call this once on startup to fetch the bulk price list
  void loadPrices();
  void reloadPrices();
  int priceCount() const { return priceMap.size(); }
  QString lastUpdated() const { return m_lastUpdated; }

  bool arePricesLoaded() const { return m_pricesLoaded; }

signals:
  void pricesLoaded();
  void pricesError(const QString &error);
  void priceFetched(const QString &skinName, double price);
  void errorOccurred(const QString &error);

private:
  void parsePrices(const QByteArray &data);
  QString m_lastUpdated;
  QNetworkAccessManager *networkManager;
  QMap<QString, double> priceMap;
  bool m_pricesLoaded = false;
  QString m_sourceUrl = "https://fursense.lol/prices.json";

  static constexpr const char *PRICES_URL = "https://fursense.lol/prices.json";
};

#endif // PRICEEMPIREAPI_H
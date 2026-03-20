#include "priceempireapi.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

PriceEmpireAPI::PriceEmpireAPI(QObject *parent)
    : QObject(parent), networkManager(new QNetworkAccessManager(this)) {}

PriceEmpireAPI::~PriceEmpireAPI() = default;

bool PriceEmpireAPI::isValid() const { return true; }

bool PriceEmpireAPI::testConnection() {
  return m_pricesLoaded && !priceMap.isEmpty();
}

void PriceEmpireAPI::loadPrices() {
  if (m_pricesLoaded)
    return;

  // Check disk cache first
  QString cacheDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QString cacheFile = cacheDir + "/prices-cache.json";
  QString sourceName = QUrl(m_sourceUrl).fileName(); // e.g. "prices.json"
  QString specificCache = cacheDir + "/cache-" + sourceName;

  QFileInfo fi(specificCache);
  bool cacheValid = fi.exists() && fi.lastModified().secsTo(
                                       QDateTime::currentDateTime()) < 6 * 3600;

  if (cacheValid) {
    qInfo() << "Using cached prices from" << specificCache;
    QFile f(specificCache);
    if (f.open(QIODevice::ReadOnly)) {
      parsePrices(f.readAll());
      f.close();
      return;
    }
  }

  qInfo() << "Fetching bulk prices from" << m_sourceUrl;

  QNetworkRequest request{QUrl(m_sourceUrl)};
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
  request.setRawHeader("Accept", "application/json");

  QNetworkReply *reply = networkManager->get(request);

  connect(
      reply, &QNetworkReply::finished, this, [this, reply, specificCache]() {
        reply->deleteLater();

        int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
          emit pricesError(
              QString("Failed to fetch prices: HTTP %1").arg(status));
          return;
        }

        QByteArray data = reply->readAll();

        // Save to cache
        QFile f(specificCache);
        if (f.open(QIODevice::WriteOnly)) {
          f.write(data);
          f.close();
        }

        parsePrices(data);
      });
}

void PriceEmpireAPI::parsePrices(const QByteArray &data) {
  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject()) {
    emit pricesError("Invalid prices JSON");
    return;
  }

  QJsonObject obj = doc.object();
  priceMap.clear();

  if (obj.contains("_updated"))
    m_lastUpdated = obj["_updated"].toString();

  for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
    // Skip metadata keys that start with underscore
    if (it.key().startsWith('_'))
      continue;
    priceMap[it.key()] = it.value().toDouble();
  }

  m_pricesLoaded = true;
  qInfo() << "Prices loaded —" << priceMap.size() << "items";
  emit pricesLoaded();
}

double PriceEmpireAPI::fetchPrice(const QString &skinName,
                                  const QString &currency, int retryCount) {
  Q_UNUSED(currency)
  Q_UNUSED(retryCount)

  if (!m_pricesLoaded) {
    qWarning() << "fetchPrice called before prices loaded for:" << skinName;
    return 0.0;
  }

  double price = priceMap.value(skinName, 0.0);

  if (price <= 0.0) {
    qWarning() << "No price found for:" << skinName;
  }

  return price;
}

void PriceEmpireAPI::reloadPrices() {
  m_pricesLoaded = false;
  loadPrices();
}

void PriceEmpireAPI::setSourceUrl(const QString &url) {
  if (m_sourceUrl == url)
    return; // no-op if same source
  m_sourceUrl = url;
  reloadPrices(); // fetch new source in background immediately
}
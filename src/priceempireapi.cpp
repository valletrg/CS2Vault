#include "priceempireapi.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <zlib.h>

static QByteArray gzipDecompress(const QByteArray &data) {
  z_stream zs = {};
  if (inflateInit2(&zs, 15 + 16) != Z_OK)
    return {};

  zs.avail_in = static_cast<uInt>(data.size());
  zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));

  QByteArray result;
  char buf[65536];
  int ret;
  do {
    zs.avail_out = sizeof(buf);
    zs.next_out = reinterpret_cast<Bytef *>(buf);
    ret = inflate(&zs, Z_NO_FLUSH);
    if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
      break;
    result.append(buf, static_cast<qsizetype>(sizeof(buf) - zs.avail_out));
  } while (ret != Z_STREAM_END && zs.avail_in > 0);

  inflateEnd(&zs);
  return result;
}

PriceEmpireAPI::PriceEmpireAPI(QObject *parent)
    : QObject(parent), networkManager(new QNetworkAccessManager(this)) {}

PriceEmpireAPI::~PriceEmpireAPI() = default;

bool PriceEmpireAPI::isValid() const { return true; }

bool PriceEmpireAPI::testConnection() {
  return m_pricesLoaded && !priceMap.isEmpty();
}

int PriceEmpireAPI::cacheTtlSeconds() const {
  if (m_sourceUrl.contains("loot.farm"))
    return 2 * 60;
  if (m_sourceUrl.contains("10min"))
    return 15 * 60;
  if (m_sourceUrl.contains("buff163"))
    return 12 * 3600;
  return 60 * 60; // white.market 1h
}

void PriceEmpireAPI::loadPrices() {
  if (m_pricesLoaded)
    return;

  // Check disk cache first
  QString cacheDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QString sourceName = QUrl(m_sourceUrl).fileName();
  QString specificCache = cacheDir + "/cache-" + sourceName;

  QFileInfo fi(specificCache);
  bool cacheValid = fi.exists() &&
                    fi.lastModified().secsTo(QDateTime::currentDateTime()) <
                        cacheTtlSeconds();

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

  if (m_sourceUrl.contains("buff163"))
    request.setRawHeader("Accept-Encoding", "gzip");

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

        // Decompress gzip if magic bytes are present
        if (data.size() >= 2 &&
            static_cast<unsigned char>(data[0]) == 0x1f &&
            static_cast<unsigned char>(data[1]) == 0x8b) {
          data = gzipDecompress(data);
        }

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
  priceMap.clear();

  if (doc.isArray()) {
    const QJsonArray arr = doc.array();
    if (!arr.isEmpty() && arr.first().toObject().contains("market_hash_name")) {
      // white.market format: [{market_hash_name, price (string)}, ...]
      for (const QJsonValue &val : arr) {
        QJsonObject entry = val.toObject();
        QString name = entry["market_hash_name"].toString();
        double price = entry["price"].toString().toDouble();
        if (!name.isEmpty() && price > 0.0)
          priceMap[name] = price;
      }
    } else {
      // Loot.Farm format: [{name, price (integer cents)}, ...]
      for (const QJsonValue &val : arr) {
        QJsonObject entry = val.toObject();
        QString name = entry["name"].toString();
        double price = entry["price"].toInt() / 100.0;
        if (!name.isEmpty() && price > 0.0)
          priceMap[name] = price;
      }
    }
  } else if (doc.isObject()) {
    // Buff163 format: {"AK-47 | Redline (FT)": {"starting_at": {"price": 2.61}, ...}}
    QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
      QJsonObject entry = it.value().toObject();
      double price = entry["starting_at"].toObject()["price"].toDouble();
      if (!it.key().isEmpty() && price > 0.0)
        priceMap[it.key()] = price;
    }
  } else {
    emit pricesError("Invalid prices JSON");
    return;
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

  // Strip trailing " (Unknown)" — pins, patches, etc. get this appended
  // and it causes price lookup misses.
  QString cleanName = skinName;
  if (cleanName.endsWith(" (Unknown)"))
    cleanName.chop(10); // length of " (Unknown)"

  double price = priceMap.value(cleanName, 0.0);

  if (price <= 0.0) {
    qWarning() << "No price found for:" << cleanName;
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
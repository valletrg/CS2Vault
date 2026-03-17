#include "priceempireapi.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QTimer>
#include <QThread>
#include <QDebug>

PriceEmpireAPI::PriceEmpireAPI(QObject *parent)
    : QObject(parent)
    , networkManager(new QNetworkAccessManager(this))
    , lastRequestTime(QDateTime::currentDateTime().addSecs(-10))
{
    // Pre-subtract 10 seconds so the very first request fires immediately
    // without any rate-limit sleep.
}

PriceEmpireAPI::~PriceEmpireAPI() = default;

bool PriceEmpireAPI::isValid() const
{
    // Steam Community Market requires no API key, so we are always ready.
    return true;
}

bool PriceEmpireAPI::testConnection()
{
    // Verify connectivity by fetching the price of a common item.
    // If we get back a non-zero price, the Steam Market endpoint is reachable.
    double price = fetchPrice("AK-47 | Redline (Field-Tested)");
    return price > 0.0;
}

void PriceEmpireAPI::rateLimit()
{
    QDateTime now = QDateTime::currentDateTime();
    qint64 elapsed = lastRequestTime.msecsTo(now);

    // Steam Market rate-limits aggressively. 1.5 seconds between requests
    // is conservative enough to avoid 429s during bulk portfolio refreshes.
    const qint64 minInterval = 1500;

    if (elapsed < minInterval) {
        qint64 sleepTime = minInterval - elapsed;
        qInfo() << "Rate limiting: sleeping for" << sleepTime << "ms";
        QThread::msleep(sleepTime);
    }

    lastRequestTime = QDateTime::currentDateTime();
}

double PriceEmpireAPI::fetchPrice(const QString &skinName, const QString &currency, int retryCount)
{
    rateLimit();

    // Steam Community Market priceoverview endpoint.
    // appid 730 = CS2 / CS:GO. currency 1 = USD.
    // market_hash_name must match Steam's exact item name, e.g.:
    //   "AK-47 | Redline (Field-Tested)"
    // Qt will percent-encode the spaces and special characters automatically
    // when we use QUrlQuery, so we don't need to manually encode anything.
    QUrl url("https://steamcommunity.com/market/priceoverview/");
    QUrlQuery query;
    query.addQueryItem("appid", "730");
    query.addQueryItem("currency", "1");
    query.addQueryItem("market_hash_name", skinName);
    url.setQuery(query);

    QNetworkRequest request{url};
    // Steam rejects requests without a browser-like User-Agent header.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = networkManager->get(request);

    // Block until the reply arrives or we time out after 10 seconds.
    // We use a local QEventLoop so the main event loop stays responsive
    // to Qt internals, but we still get synchronous behaviour here.
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();

    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray data = reply->readAll();
    reply->deleteLater();

    // 429 = Too Many Requests. Back off and retry, but cap retries at 3
    // to avoid infinite recursion if Steam is having a bad day.
    if (httpStatus == 429) {
        if (retryCount >= 3) {
            qWarning() << "Rate limited 3 times for" << skinName << "- giving up";
            return 0.0;
        }
        qWarning() << "Rate limited (429). Backing off 3 seconds... (attempt" << retryCount + 1 << ")";
        QThread::msleep(3000);
        return fetchPrice(skinName, currency, retryCount + 1);
    }

    if (httpStatus != 200) {
        qWarning() << "HTTP" << httpStatus << "fetching price for:" << skinName;
        return 0.0;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        qWarning() << "Unexpected response format for:" << skinName;
        return 0.0;
    }

    QJsonObject obj = doc.object();

    // Steam returns { "success": true, "lowest_price": "$1.23", "volume": "123",
    //                 "median_price": "$1.10" }
    // Prices are formatted strings with a currency symbol - we need to strip
    // that before calling toDouble(). We also remove commas for prices >= $1,000.
   
    qDebug() << "HTTP status for" << skinName << ":" << httpStatus;
    qDebug() << "Raw response for" << skinName << ":" << data;
    qDebug() << "Raw response for" << skinName << ":" << data;

    if (!obj["success"].toBool()) {
        qWarning() << "Steam returned success:false for:" << skinName;
        return 0.0;
}

    // Prefer lowest_price (cheapest active listing), fall back to median_price.
    QString priceStr = obj["lowest_price"].toString();
    if (priceStr.isEmpty()) {
        priceStr = obj["median_price"].toString();
    }

    if (priceStr.isEmpty()) {
        qWarning() << "No active Steam Market listings for:" << skinName;
        return 0.0;
    }

    // Strip everything that isn't a digit or a decimal point.
    // This handles "$", "€", "£", commas, and any other locale-specific formatting.
    priceStr.remove(QRegularExpression("[^0-9.]"));
    double price = priceStr.toDouble();

    if (price <= 0.0) {
        qWarning() << "Parsed price is zero for:" << skinName << "(raw:" << priceStr << ")";
    }

    return price;
}
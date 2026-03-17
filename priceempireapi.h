#ifndef PRICEEMPIREAPI_H
#define PRICEEMPIREAPI_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>

class PriceEmpireAPI : public QObject
{
    Q_OBJECT

public:
    explicit PriceEmpireAPI(QObject *parent = nullptr);
    ~PriceEmpireAPI();

    // Always returns true - Steam Market needs no API key
    bool isValid() const;

    // Tests connectivity by fetching a known item price
    bool testConnection();

    // Fetches the lowest listing price from Steam Community Market.
    // skinName should be the market_hash_name e.g. "AK-47 | Redline (Field-Tested)"
    // currency parameter is kept for API compatibility but currently unused (USD only)
    double fetchPrice(const QString &skinName, const QString &currency = "USD", int retryCount = 0);

    signals:
        void priceFetched(const QString &skinName, double price);
    void errorOccurred(const QString &error);

private:
    void rateLimit();

    QNetworkAccessManager *networkManager;
    QDateTime lastRequestTime;
};

#endif // PRICEEMPIREAPI_H
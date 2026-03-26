#ifndef PORTFOLIOMANAGER_H
#define PORTFOLIOMANAGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

struct PortfolioHistoryPoint {
    qint64 timestamp;
    double totalValue;
    double totalCost;
};

struct PortfolioItem {
    QString skinName;
    QString condition;
    double floatValue;
    int quantity;
    double buyPrice;
    double currentPrice;
    QString purchaseDate;
    QString notes;
    QString iconUrl;
    QString rarity;
};

struct Portfolio {
    QString id;
    QString name;
    QString description;
    QVector<PortfolioItem> items;
    QVector<PortfolioHistoryPoint> history;
    QString createdAt;
    QString updatedAt;
};

class PortfolioManager : public QObject
{
    Q_OBJECT

public:
    explicit PortfolioManager(QObject *parent = nullptr);
    ~PortfolioManager();

    QString createPortfolio(const QString &name, const QString &description = "");
    bool deletePortfolio(const QString &id);
    bool renamePortfolio(const QString &id, const QString &newName);
    QVector<Portfolio> getAllPortfolios() const;
    Portfolio getPortfolio(const QString &id) const;
    bool portfolioExists(const QString &id) const;
    void recordHistoryPoint(const QString &id, bool forced = false);

    void addItem(const QString &portfolioId, const PortfolioItem &item);
    bool removeItem(const QString &portfolioId, int index);
    void updateItem(const QString &portfolioId, int index, const PortfolioItem &item);
    void clearPortfolio(const QString &portfolioId);
    void importFromSteamInventory(const QString &portfolioId, const QVector<PortfolioItem> &items);

    void saveToFile();
    void loadFromFile();

signals:
    void portfolioCreated(const QString &id, const QString &name);
    void portfolioDeleted(const QString &id);
    void portfolioRenamed(const QString &id, const QString &newName);
    void itemAdded(const QString &portfolioId, const PortfolioItem &item);
    void itemRemoved(const QString &portfolioId, int index);
    void portfolioChanged(const QString &portfolioId);

private:
    QString generateId() const;
    QString getCurrentTimestamp() const;
    PortfolioItem itemFromJson(const QJsonObject &obj) const;
    QJsonObject itemToJson(const PortfolioItem &item) const;

    QMap<QString, Portfolio> portfolios;
    QString dataFile;
};

#endif // PORTFOLIOMANAGER_H

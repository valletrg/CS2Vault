#ifndef DASHBOARDWIDGET_H
#define DASHBOARDWIDGET_H

#include "accountmanager.h"
#include "priceempireapi.h"
#include "steamapi.h"
#include "steamcompanion.h"

#include <QFrame>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QTableWidget>
#include <QWidget>

class DashboardWidget : public QWidget {
  Q_OBJECT

public:
  explicit DashboardWidget(SteamCompanion *companion, PriceEmpireAPI *api,
                           SteamAPI *steamApi, AccountManager *accountManager,
                           QWidget *parent = nullptr);

private slots:
  void onProfileLoaded(const SteamProfile &profile);
  void onInventoryReceived(const QList<GCItem> &items,
                           const QList<GCContainer> &containers);
  void onStorageUnitReceived(const QString &casketId,
                             const QList<GCItem> &items);
  void onPricesLoaded();

private:
  void setupUI();
  QFrame *createValueCard(const QString &title, QLabel **valueLabel,
                          QLabel **countLabel);
  void recalculate();
  void rebuildTopItems();
  void updateGreeting(const QString &name);
  void fetchAvatar(const QString &url);

  // Inventory cache (AppDataLocation/inventory-cache-<accountId>.json)
  QString cacheFilePath() const;
  void loadCache();
  void saveCache();

  SteamCompanion *companion;
  PriceEmpireAPI *api;
  SteamAPI *steamApi;
  AccountManager *accountManager;
  QNetworkAccessManager *nam;

  // Header
  QLabel *avatarLabel = nullptr;
  QLabel *greetingLabel = nullptr;
  QLabel *steamIdLabel = nullptr;

  // Value cards
  QLabel *totalValueLabel = nullptr;
  QLabel *totalCountLabel = nullptr;
  QLabel *storageValueLabel = nullptr;
  QLabel *storageCountLabel = nullptr;
  QLabel *inventoryValueLabel = nullptr;
  QLabel *inventoryCountLabel = nullptr;

  // Top items table
  QTableWidget *topItemsTable = nullptr;

  // Status
  QLabel *statusLabel = nullptr;

  // Data
  QList<GCItem> inventoryItems;
  QList<GCContainer> containers;
  QMap<QString, QList<GCItem>> storageItems; // casketId → items
  QMap<QString, QString> containerNames;     // casketId → display name

  double inventoryValue = 0.0;
  double storageValue = 0.0;
  int inventoryCount = 0;
  int storageCount = 0;

  // Per-item prices from the last time we had both live inventory + live
  // prices. Used as a fallback when prices haven't loaded yet on this launch.
  QMap<QString, double> cachedPrices;
};

#endif // DASHBOARDWIDGET_H

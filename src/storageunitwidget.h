#ifndef STORAGEUNITWIDGET_H
#define STORAGEUNITWIDGET_H

#include "priceempireapi.h"
#include "steamcompanion.h"
#include "tradehistory.h"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QWidget>

class StorageUnitWidget : public QWidget {
  Q_OBJECT

public:
  explicit StorageUnitWidget(SteamCompanion *companion, PriceEmpireAPI *api,
                             TradeHistoryManager *tradeHistoryManager = nullptr,
                             QWidget *parent = nullptr);
  ~StorageUnitWidget();

public slots:
  void onContainersUpdated(const QList<GCContainer> &containers);

private slots:
  void onStorageUnitSelected(int index);
  void onRefreshClicked();
  void onMoveToInventory();
  void onMoveToStorageUnit();
  void onTransferComplete(const QString &action, const QString &casketId,
                          const QString &itemId);

private:
  void setupUI();
  void populateStorageTable(const QList<GCItem> &items);
  void populateInventoryTable(const QList<GCItem> &items);
  void setStatus(const QString &text, const QString &color = "#aaa");
  void setBusy(bool busy);

  QLabel *storageStatsLabel = nullptr;
  PriceEmpireAPI *api = nullptr;
  TradeHistoryManager *tradeHistoryManager = nullptr;
  void updateStorageStats();

  SteamCompanion *companion;

  // Top bar
  QComboBox *storageCombo;
  QPushButton *refreshButton;
  QLabel *statusLabel;

  // Left — storage unit
  QTableWidget *storageTable;
  QPushButton *storageSelectAllButton;
  QPushButton *storageSelectNoneButton;
  QPushButton *moveToInventoryButton;

  // Right — inventory
  QTableWidget *inventoryTable;
  QPushButton *inventorySelectAllButton;
  QPushButton *inventorySelectNoneButton;
  QPushButton *moveToStorageButton;
  QLineEdit *inventorySearchEdit;

  // State
  QList<GCContainer> containers;
  QList<GCItem> storageItems;
  QList<GCItem> inventoryItems;
  QString currentCasketId;
};

#endif // STORAGEUNITWIDGET_H
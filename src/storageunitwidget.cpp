#include "storageunitwidget.h"

#include <QDateTime>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QSet>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

StorageUnitWidget::StorageUnitWidget(SteamCompanion *companion,
                                     PriceEmpireAPI *api,
                                     TradeHistoryManager *tradeHistoryManager,
                                     QWidget *parent)
    : QWidget(parent), companion(companion), api(api),
      tradeHistoryManager(tradeHistoryManager) {
  setupUI();

  connect(companion, &SteamCompanion::gcReady, this, [this]() {
    setStatus("GC ready — loading inventory...", "#5A9BE6");
    this->companion->requestInventory();
  });

  connect(
      companion, &SteamCompanion::inventoryReceived, this,
      [this](const QList<GCItem> &items,
             const QList<GCContainer> &newContainers) {
        if (!items.isEmpty()) {
          inventoryItems = items;
          populateInventoryTable(items);
        }
        if (!newContainers.isEmpty()) {
          onContainersUpdated(newContainers);
        }
        if (!items.isEmpty()) {
          setStatus(
              QString(
                  "Inventory loaded — %1 items. Select a storage unit above.")
                  .arg(items.size()),
              "#38C878");
        }
        setBusy(false);
      });

  connect(companion, &SteamCompanion::storageUnitReceived, this,
          [this](const QString &casketId, const QList<GCItem> &items) {
            currentCasketId = casketId;
            storageItems = items;
            populateStorageTable(items);
            setStatus(
                QString("Storage unit loaded — %1 item(s).").arg(items.size()),
                "#38C878");
            setBusy(false);
          });

  connect(companion, &SteamCompanion::transferComplete, this,
          &StorageUnitWidget::onTransferComplete);

  connect(companion, &SteamCompanion::errorOccurred, this,
          [this](const QString &err) {
            setStatus("Error: " + err, "#DC4646");
            setBusy(false);
          });
}

StorageUnitWidget::~StorageUnitWidget() = default;

void StorageUnitWidget::setupUI() {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(8);

  // ── Top bar ───────────────────────────────────────────────────────────────
  QHBoxLayout *topBar = new QHBoxLayout();

  QLabel *selectLabel = new QLabel("Storage Unit:", this);
  storageCombo = new QComboBox(this);
  storageCombo->setMinimumWidth(240);
  storageCombo->setPlaceholderText("Waiting for GC connection...");
  storageCombo->setEnabled(false);

  refreshButton = new QPushButton("↺  Refresh", this);
  refreshButton->setFixedWidth(100);
  refreshButton->setFixedHeight(30);

  statusLabel =
      new QLabel("Sign in to load your inventory and storage units.", this);
  statusLabel->setStyleSheet("color: #888; font-size: 11px;");

  topBar->addWidget(selectLabel);
  topBar->addWidget(storageCombo, 1);
  topBar->addWidget(refreshButton);
  topBar->addStretch();
  topBar->addWidget(statusLabel);
  mainLayout->addLayout(topBar);

  // ── Split view ────────────────────────────────────────────────────────────
  QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

  // ── Left — Storage unit ───────────────────────────────────────────────────
  QWidget *leftWidget = new QWidget(splitter);
  QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);
  leftLayout->setContentsMargins(0, 0, 4, 0);
  leftLayout->setSpacing(6);

  QHBoxLayout *leftHeader = new QHBoxLayout();
  QLabel *storageLabel = new QLabel("Storage Unit Contents", leftWidget);
  QFont boldFont;
  boldFont.setBold(true);
  storageLabel->setFont(boldFont);

  storageSelectAllButton = new QPushButton("Select All", leftWidget);
  storageSelectNoneButton = new QPushButton("Select None", leftWidget);
  storageSelectAllButton->setFixedHeight(26);
  storageSelectNoneButton->setFixedHeight(26);

  leftHeader->addWidget(storageLabel);
  leftHeader->addStretch();
  leftHeader->addWidget(storageSelectAllButton);
  leftHeader->addWidget(storageSelectNoneButton);
  leftLayout->addLayout(leftHeader);

  storageTable = new QTableWidget(leftWidget);
  storageTable->setColumnCount(3);
  storageTable->setHorizontalHeaderLabels({"Item", "Wear", "Float"});
  storageTable->horizontalHeader()->setSectionResizeMode(0,
                                                         QHeaderView::Stretch);
  storageTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  storageTable->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  storageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  storageTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
  storageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  storageTable->setAlternatingRowColors(true);
  storageTable->verticalHeader()->setVisible(false);
  storageTable->setSortingEnabled(true);
  leftLayout->addWidget(storageTable);
  storageStatsLabel = new QLabel("Select a storage unit to see stats", this);
  storageStatsLabel->setStyleSheet("color: #4fc3f7; font-size: 11px;");
  topBar->addWidget(storageStatsLabel);

  moveToInventoryButton =
      new QPushButton("→  Move Selected to Inventory", leftWidget);
  moveToInventoryButton->setFixedHeight(36);
  moveToInventoryButton->setEnabled(false);
  leftLayout->addWidget(moveToInventoryButton);

  // ── Right — Inventory ─────────────────────────────────────────────────────
  QWidget *rightWidget = new QWidget(splitter);
  QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
  rightLayout->setContentsMargins(4, 0, 0, 0);
  rightLayout->setSpacing(6);

  QHBoxLayout *rightHeader = new QHBoxLayout();
  QLabel *inventoryLabel = new QLabel("Your Inventory", rightWidget);
  inventoryLabel->setFont(boldFont);

  inventorySearchEdit = new QLineEdit(rightWidget);
  inventorySearchEdit->setPlaceholderText("Search...");
  inventorySearchEdit->setFixedHeight(26);
  inventorySearchEdit->setMaximumWidth(160);

  inventorySelectAllButton = new QPushButton("Select All", rightWidget);
  inventorySelectNoneButton = new QPushButton("Select None", rightWidget);
  inventorySelectAllButton->setFixedHeight(26);
  inventorySelectNoneButton->setFixedHeight(26);

  rightHeader->addWidget(inventoryLabel);
  rightHeader->addStretch();
  rightHeader->addWidget(inventorySearchEdit);
  rightHeader->addWidget(inventorySelectAllButton);
  rightHeader->addWidget(inventorySelectNoneButton);
  rightLayout->addLayout(rightHeader);

  inventoryTable = new QTableWidget(rightWidget);
  inventoryTable->setColumnCount(3);
  inventoryTable->setHorizontalHeaderLabels({"Item", "Wear", "Float"});
  inventoryTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  inventoryTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  inventoryTable->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  inventoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  inventoryTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
  inventoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  inventoryTable->setAlternatingRowColors(true);
  inventoryTable->verticalHeader()->setVisible(false);
  inventoryTable->setSortingEnabled(true);
  rightLayout->addWidget(inventoryTable);

  moveToStorageButton =
      new QPushButton("←  Move Selected to Storage Unit", rightWidget);
  moveToStorageButton->setFixedHeight(36);
  moveToStorageButton->setEnabled(false);
  rightLayout->addWidget(moveToStorageButton);

  splitter->addWidget(leftWidget);
  splitter->addWidget(rightWidget);
  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 1);
  mainLayout->addWidget(splitter, 1);

  // ── Connections ───────────────────────────────────────────────────────────
  connect(storageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &StorageUnitWidget::onStorageUnitSelected);
  connect(refreshButton, &QPushButton::clicked, this,
          &StorageUnitWidget::onRefreshClicked);
  connect(moveToInventoryButton, &QPushButton::clicked, this,
          &StorageUnitWidget::onMoveToInventory);
  connect(moveToStorageButton, &QPushButton::clicked, this,
          &StorageUnitWidget::onMoveToStorageUnit);

  connect(storageSelectAllButton, &QPushButton::clicked, storageTable,
          &QTableWidget::selectAll);
  connect(storageSelectNoneButton, &QPushButton::clicked, storageTable,
          &QTableWidget::clearSelection);
  connect(inventorySelectAllButton, &QPushButton::clicked, inventoryTable,
          &QTableWidget::selectAll);
  connect(inventorySelectNoneButton, &QPushButton::clicked, inventoryTable,
          &QTableWidget::clearSelection);

  connect(storageTable->selectionModel(),
          &QItemSelectionModel::selectionChanged, this, [this]() {
            moveToInventoryButton->setEnabled(
                !storageTable->selectedItems().isEmpty() &&
                !currentCasketId.isEmpty());
          });
  connect(inventoryTable->selectionModel(),
          &QItemSelectionModel::selectionChanged, this, [this]() {
            moveToStorageButton->setEnabled(
                !inventoryTable->selectedItems().isEmpty() &&
                !currentCasketId.isEmpty());
          });

  connect(inventorySearchEdit, &QLineEdit::textChanged, this,
          [this](const QString &text) {
            for (int i = 0; i < inventoryTable->rowCount(); ++i) {
              QTableWidgetItem *item = inventoryTable->item(i, 0);
              bool match =
                  !item || item->text().contains(text, Qt::CaseInsensitive);
              inventoryTable->setRowHidden(i, !match);
            }
          });
}

void StorageUnitWidget::onContainersUpdated(
    const QList<GCContainer> &newContainers) {
  containers = newContainers;
  storageCombo->blockSignals(true);
  storageCombo->clear();
  storageCombo->addItem("— Select a storage unit —"); // placeholder
  for (const GCContainer &c : containers)
    storageCombo->addItem(c.name, c.id);
  storageCombo->setEnabled(!containers.isEmpty());
  storageCombo->blockSignals(false);

  // Don't auto-select — wait for user to pick
  storageCombo->setCurrentIndex(0);
  setStatus(QString("%1 storage unit(s) found. Select one above.")
                .arg(containers.size()),
            "#38C878");
}

void StorageUnitWidget::onStorageUnitSelected(int index) {
  if (index <= 0 || index > containers.size())
    return;

  currentCasketId = containers[index - 1].id;
  storageTable->setRowCount(0);
  setStatus(QString("Loading %1...").arg(containers[index - 1].name),
            "#5A9BE6");
  setBusy(true);
  companion->requestStorageUnit(currentCasketId);
}

void StorageUnitWidget::onRefreshClicked() {
  storageTable->setRowCount(0);
  inventoryTable->setRowCount(0);
  setStatus("Refreshing...", "#5A9BE6");
  setBusy(true);
  companion->requestInventory();
  if (!currentCasketId.isEmpty())
    companion->requestStorageUnit(currentCasketId);
}

void StorageUnitWidget::onMoveToInventory() {
  QList<QTableWidgetItem *> selected = storageTable->selectedItems();
  if (selected.isEmpty() || currentCasketId.isEmpty())
    return;

  QSet<int> rows;
  for (QTableWidgetItem *item : selected)
    rows.insert(item->row());

  int count = rows.size();
  if (QMessageBox::question(
          this, "Confirm Move",
          QString("Move %1 item(s) from storage unit to your inventory?")
              .arg(count),
          QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    return;

  setBusy(true);
  pendingTransfers = count;
  totalTransfers = count;
  setStatus(QString("Moving %1 item(s) to inventory... (0/%1)").arg(count),
            "#F0A500");

  for (int row : rows) {
    if (row < storageItems.size())
      companion->removeFromStorageUnit(currentCasketId, storageItems[row].id);
  }
}

void StorageUnitWidget::onMoveToStorageUnit() {
  QList<QTableWidgetItem *> selected = inventoryTable->selectedItems();
  if (selected.isEmpty() || currentCasketId.isEmpty())
    return;

  QSet<int> rows;
  for (QTableWidgetItem *item : selected)
    rows.insert(item->row());

  int count = rows.size();
  if (QMessageBox::question(this, "Confirm Move",
                            QString("Move %1 item(s) to storage unit '%2'?")
                                .arg(count)
                                .arg(storageCombo->currentText()),
                            QMessageBox::Yes | QMessageBox::No) !=
      QMessageBox::Yes)
    return;

  setBusy(true);
  pendingTransfers = count;
  totalTransfers = count;
  setStatus(
      QString("Moving %1 item(s) to storage unit... (0/%1)").arg(count),
      "#F0A500");

  for (int row : rows) {
    if (row < inventoryItems.size())
      companion->addToStorageUnit(currentCasketId, inventoryItems[row].id);
  }
}

void StorageUnitWidget::onTransferComplete(const QString &action,
                                           const QString &casketId,
                                           const QString &itemId) {
  Q_UNUSED(casketId)

  // Log to trade history
  if (tradeHistoryManager) {
    // Find the item name from our cached lists
    QString itemName;
    auto findItem = [&](const QList<GCItem> &list) -> QString {
      for (const GCItem &item : list) {
        if (item.id == itemId) {
          return item.marketHashName.isEmpty() ? item.name
                                               : item.marketHashName;
        }
      }
      return {};
    };

    if (action == "added")
      itemName = findItem(inventoryItems);
    else
      itemName = findItem(storageItems);

    if (!itemName.isEmpty()) {
      TradeHistoryEntry entry;
      entry.itemName = itemName;
      entry.type = (action == "added") ? "storage_in" : "storage_out";
      entry.timestamp = QDateTime::currentMSecsSinceEpoch();
      entry.storageUnit = storageCombo->currentText();
      tradeHistoryManager->addEntry(entry);
    }
  }

  if (pendingTransfers > 0)
    --pendingTransfers;

  int done = totalTransfers - pendingTransfers;
  if (pendingTransfers > 0) {
    setStatus(
        QString(action == "added" ? "Moving to storage unit... (%1/%2)"
                                  : "Moving to inventory... (%1/%2)")
            .arg(done)
            .arg(totalTransfers),
        "#F0A500");
    return;
  }

  // All items in the batch are done — do a single refresh
  setStatus(action == "added" ? "All items moved to storage unit. Refreshing..."
                              : "All items moved to inventory. Refreshing...",
            "#38C878");

  QTimer::singleShot(1500, this, [this]() {
    companion->requestInventory();
    if (!currentCasketId.isEmpty())
      companion->requestStorageUnit(currentCasketId);
  });
}

void StorageUnitWidget::populateStorageTable(const QList<GCItem> &items) {
  storageTable->setSortingEnabled(false);
  storageTable->setRowCount(items.size());
  for (int i = 0; i < items.size(); ++i) {
    const GCItem &item = items[i];
    storageTable->setItem(i, 0, new QTableWidgetItem(item.name));
    storageTable->setItem(
        i, 1,
        new QTableWidgetItem(item.exterior.isEmpty() ? "—" : item.exterior));
    storageTable->setItem(
        i, 2,
        new QTableWidgetItem(item.paintWear > 0
                                 ? QString::number(item.paintWear, 'f', 6)
                                 : "—"));
  }
  storageTable->setSortingEnabled(true);
  moveToInventoryButton->setEnabled(false);

  // Update stats
  updateStorageStats();
}

void StorageUnitWidget::updateStorageStats() {
  if (storageItems.isEmpty()) {
    storageStatsLabel->setText("Empty");
    return;
  }

  double totalValue = 0.0;
  int priced = 0;

  for (const GCItem &item : storageItems) {
    if (!item.marketHashName.isEmpty()) {
      double price = api->fetchPrice(item.marketHashName);
      if (price > 0.0) {
        totalValue += price;
        priced++;
      }
    }
  }

  QString stats = QString("%1 items").arg(storageItems.size());
  if (priced > 0)
    stats += QString("  ·  $%1 total value (%2 priced)")
                 .arg(totalValue, 0, 'f', 2)
                 .arg(priced);
  else
    stats += "  ·  Load prices to see value";

  storageStatsLabel->setText(stats);
}

void StorageUnitWidget::populateInventoryTable(const QList<GCItem> &items) {
  inventoryTable->setSortingEnabled(false);
  inventoryTable->setRowCount(items.size());
  for (int i = 0; i < items.size(); ++i) {
    const GCItem &item = items[i];
    inventoryTable->setItem(i, 0, new QTableWidgetItem(item.name));
    inventoryTable->setItem(
        i, 1,
        new QTableWidgetItem(item.exterior.isEmpty() ? "—" : item.exterior));
    inventoryTable->setItem(
        i, 2,
        new QTableWidgetItem(item.paintWear > 0
                                 ? QString::number(item.paintWear, 'f', 6)
                                 : "—"));
  }
  inventoryTable->setSortingEnabled(true);
  moveToStorageButton->setEnabled(false);

  const QString search = inventorySearchEdit->text();
  if (!search.isEmpty()) {
    for (int i = 0; i < inventoryTable->rowCount(); ++i) {
      QTableWidgetItem *it = inventoryTable->item(i, 0);
      inventoryTable->setRowHidden(
          i, it && !it->text().contains(search, Qt::CaseInsensitive));
    }
  }
}

void StorageUnitWidget::setStatus(const QString &text, const QString &color) {
  statusLabel->setText(text);
  statusLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(color));
}

void StorageUnitWidget::setBusy(bool busy) {
  storageCombo->setEnabled(!busy && !containers.isEmpty());
  refreshButton->setEnabled(!busy);
  moveToInventoryButton->setEnabled(!busy &&
                                    !storageTable->selectedItems().isEmpty());
  moveToStorageButton->setEnabled(!busy &&
                                  !inventoryTable->selectedItems().isEmpty());
}
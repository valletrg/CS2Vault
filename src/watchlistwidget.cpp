#include "watchlistwidget.h"
#include "priceempireapi.h"
#include "watchlistmanager.h"

#include <QComboBox>
#include <QDate>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QSplitter>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Helper: numeric sort for table columns that contain formatted numbers
// ---------------------------------------------------------------------------
class WatchlistNumericItem : public QTableWidgetItem {
public:
  using QTableWidgetItem::QTableWidgetItem;
  bool operator<(const QTableWidgetItem &other) const override {
    QVariant a = data(Qt::UserRole);
    QVariant b = other.data(Qt::UserRole);
    if (a.isValid() && b.isValid())
      return a.toDouble() < b.toDouble();
    return QTableWidgetItem::operator<(other);
  }
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
WatchlistWidget::WatchlistWidget(PriceEmpireAPI *api,
                                 WatchlistManager *watchlistManager,
                                 QWidget *parent)
    : QWidget(parent), api(api), watchlistManager(watchlistManager),
      autoSaveTimer(new QTimer(this)) {
  setupUI();

  connect(watchlistManager, &WatchlistManager::watchlistChanged, this,
          [this](const QString &id) {
            if (id == currentWatchlistId && watchlistTable)
              loadWatchlistItems();
          });

  refreshWatchlistList();

  connect(autoSaveTimer, &QTimer::timeout, watchlistManager,
          &WatchlistManager::saveToFile);
  autoSaveTimer->start(30000);
}

WatchlistWidget::~WatchlistWidget() = default;

// ---------------------------------------------------------------------------
// setupUI
// ---------------------------------------------------------------------------
void WatchlistWidget::setupUI() {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(6);
  mainLayout->setContentsMargins(8, 8, 8, 8);

  // ── Watchlist selector row ──────────────────────────────────────────────
  QHBoxLayout *selectLayout = new QHBoxLayout();
  selectLayout->setSpacing(4);

  QLabel *wlLabel = new QLabel("Watchlist:", this);
  watchlistComboBox = new QComboBox(this);
  watchlistComboBox->setMinimumWidth(200);

  createButton = new QPushButton("New", this);
  deleteButton = new QPushButton("Delete", this);
  renameButton = new QPushButton("Rename", this);

  for (auto *btn : {createButton, renameButton, deleteButton})
    btn->setFixedHeight(26);

  selectLayout->addWidget(wlLabel);
  selectLayout->addWidget(watchlistComboBox, 1);
  selectLayout->addWidget(createButton);
  selectLayout->addWidget(renameButton);
  selectLayout->addWidget(deleteButton);
  mainLayout->addLayout(selectLayout);

  connect(watchlistComboBox,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &WatchlistWidget::onWatchlistSelected);
  connect(createButton, &QPushButton::clicked, this,
          &WatchlistWidget::onCreateWatchlist);
  connect(deleteButton, &QPushButton::clicked, this,
          &WatchlistWidget::onDeleteWatchlist);
  connect(renameButton, &QPushButton::clicked, this,
          &WatchlistWidget::onRenameWatchlist);

  // ── Stats bar ───────────────────────────────────────────────────────────
  QWidget *statsBar = new QWidget(this);
  statsBar->setStyleSheet("QWidget { background: #1e2130; border: 1px solid "
                          "#373a48; border-radius: 6px; }"
                          "QLabel  { border: none; background: transparent; }");
  QHBoxLayout *statsLayout = new QHBoxLayout(statsBar);
  statsLayout->setContentsMargins(12, 6, 12, 6);
  statsLayout->setSpacing(24);

  auto makeStatPair = [&](const QString &title) -> QLabel * {
    QVBoxLayout *col = new QVBoxLayout();
    col->setSpacing(1);
    QLabel *titleLbl = new QLabel(title, statsBar);
    titleLbl->setStyleSheet("color: #888; font-size: 10px;");
    QLabel *valueLbl =
        new QLabel(QString::fromUtf8("\xe2\x80\x94"), statsBar);
    valueLbl->setStyleSheet(
        "color: #e0e0e0; font-size: 14px; font-weight: bold;");
    col->addWidget(titleLbl);
    col->addWidget(valueLbl);
    statsLayout->addLayout(col);
    return valueLbl;
  };

  itemCountLabel = makeStatPair("ITEMS");
  totalValueLabel = makeStatPair("TOTAL VALUE");
  avgPriceLabel = makeStatPair("AVG PRICE");
  topMoverLabel = makeStatPair("TOP MOVER (24H)");
  statsLayout->addStretch();
  mainLayout->addWidget(statsBar);

  // ── Table ───────────────────────────────────────────────────────────────
  watchlistTable = new QTableWidget(this);
  watchlistTable->setColumnCount(6);
  watchlistTable->setHorizontalHeaderLabels(
      {"Item Name", "Condition", "Current Price", "24h %", "7d %", "30d %"});
  watchlistTable->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Interactive);
  watchlistTable->horizontalHeader()->setStretchLastSection(true);
  watchlistTable->horizontalHeader()->setMinimumSectionSize(60);
  watchlistTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  watchlistTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  watchlistTable->setSortingEnabled(true);
  watchlistTable->setMinimumHeight(180);
  connect(watchlistTable, &QTableWidget::cellClicked, this,
          &WatchlistWidget::onItemClicked);
  mainLayout->addWidget(watchlistTable, 1);

  // Enable/disable remove based on selection
  connect(watchlistTable, &QTableWidget::currentCellChanged, this,
          [this](int row, int, int, int) {
            removeButton->setEnabled(row >= 0);
          });

  // ── Action buttons ──────────────────────────────────────────────────────
  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(4);

  addButton = new QPushButton("Add Item", this);
  removeButton = new QPushButton("Remove", this);
  refreshButton = new QPushButton("Refresh Prices", this);

  removeButton->setEnabled(false);

  for (auto *btn : {addButton, removeButton, refreshButton}) {
    btn->setFixedHeight(28);
    buttonLayout->addWidget(btn);
  }
  buttonLayout->addStretch();
  mainLayout->addLayout(buttonLayout);

  connect(addButton, &QPushButton::clicked, this,
          &WatchlistWidget::onAddItem);
  connect(removeButton, &QPushButton::clicked, this,
          &WatchlistWidget::onRemoveItem);
  connect(refreshButton, &QPushButton::clicked, this,
          &WatchlistWidget::onRefreshPrices);

  // ── Chart section ───────────────────────────────────────────────────────
  chartContainer = new QWidget(this);
  QVBoxLayout *chartContainerLayout = new QVBoxLayout(chartContainer);
  chartContainerLayout->setContentsMargins(0, 4, 0, 0);
  chartContainerLayout->setSpacing(6);

  // Item label above chart
  chartItemLabel = new QLabel("", chartContainer);
  chartItemLabel->setStyleSheet(
      "color: #e0e0e0; font-size: 12px; font-weight: bold;");
  chartItemLabel->hide();
  chartContainerLayout->addWidget(chartItemLabel);

  // Time range selector bar
  QHBoxLayout *timeRangeLayout = new QHBoxLayout();
  timeRangeLayout->setSpacing(2);
  timeRangeLayout->setContentsMargins(0, 0, 0, 0);
  QStringList ranges = {"24H", "7D", "1M", "3M", "6M", "1Y", "All"};
  for (const QString &r : ranges) {
    auto *btn = new QPushButton(r, chartContainer);
    btn->setCheckable(true);
    btn->setFixedHeight(26);
    btn->setMinimumWidth(36);
    btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: "
        "12px;"
        "              color: #8b8fa3; font-size: 11px; font-weight: bold; }"
        "QPushButton:checked { background: #2a2d3a; color: #e0e0e0; }"
        "QPushButton:hover:!checked { color: #c0c4d8; }");
    if (r == selectedTimeRange)
      btn->setChecked(true);
    connect(btn, &QPushButton::clicked, this,
            [this, r]() { onTimeRangeChanged(r); });
    timeRangeLayout->addWidget(btn);
    timeRangeButtons.append(btn);
  }
  timeRangeLayout->addStretch();
  chartContainerLayout->addLayout(timeRangeLayout);

  // Chart
  priceChart = new QChart();
  priceChart->setTitle("");
  priceChart->setAnimationOptions(QChart::NoAnimation);
  priceChart->legend()->hide();
  priceChart->setBackgroundBrush(QBrush(QColor(0x0f, 0x11, 0x17)));
  priceChart->setBackgroundPen(Qt::NoPen);
  priceChart->setPlotAreaBackgroundBrush(QBrush(QColor(0x0f, 0x11, 0x17)));
  priceChart->setPlotAreaBackgroundVisible(true);
  priceChart->setMargins(QMargins(0, 8, 0, 0));

  priceSeries = new QLineSeries();
  QPen seriesPen(QColor(0x4f, 0xc3, 0xf7));
  seriesPen.setWidth(2);
  priceSeries->setPen(seriesPen);

  priceChart->addSeries(priceSeries);

  axisX = new QDateTimeAxis();
  axisX->setTickCount(6);
  axisX->setFormat("MMM d");
  axisX->setTitleText("");
  axisX->setLabelsFont(QFont("Segoe UI", 8));
  axisX->setLabelsColor(QColor(0x64, 0x6e, 0x87));
  axisX->setLinePen(QPen(QColor(0x1e, 0x22, 0x33)));
  axisX->setGridLineVisible(false);
  axisX->setShadesVisible(false);
  priceChart->addAxis(axisX, Qt::AlignBottom);
  priceSeries->attachAxis(axisX);

  axisY = new QValueAxis();
  axisY->setLabelFormat("$%.2f");
  axisY->setTitleText("");
  axisY->setLabelsFont(QFont("Segoe UI", 8));
  axisY->setLabelsColor(QColor(0x64, 0x6e, 0x87));
  axisY->setLinePen(QPen(QColor(0x1e, 0x22, 0x33)));
  axisY->setGridLineColor(QColor(0x1e, 0x22, 0x33));
  axisY->setMinorGridLineVisible(false);
  axisY->setShadesVisible(false);
  priceChart->addAxis(axisY, Qt::AlignRight);
  priceSeries->attachAxis(axisY);

  chartView = new QChartView(priceChart);
  chartView->setRenderHint(QPainter::Antialiasing);
  chartView->setMinimumHeight(250);
  chartView->setStyleSheet("background: #0f1117; border: none;");

  // Badge label overlaid on chart
  priceBadge = new QLabel(chartView);
  priceBadge->setStyleSheet(
      "background: #4fc3f7; color: #0f1117; font-size: 10px; font-weight: "
      "bold;"
      " padding: 2px 6px; border-radius: 3px;");
  priceBadge->setFixedHeight(18);
  priceBadge->hide();

  // Placeholder
  chartPlaceholder = new QLabel(
      "Select an item to view its price history.", chartContainer);
  chartPlaceholder->setAlignment(Qt::AlignCenter);
  chartPlaceholder->setStyleSheet(
      "color: #555; font-size: 12px; padding: 40px;");

  chartContainerLayout->addWidget(chartView);
  chartContainerLayout->addWidget(chartPlaceholder);
  chartView->hide();
  mainLayout->addWidget(chartContainer);
}

// ---------------------------------------------------------------------------
// Watchlist selector
// ---------------------------------------------------------------------------
void WatchlistWidget::refreshWatchlistList() {
  watchlistComboBox->blockSignals(true);
  watchlistComboBox->clear();

  QVector<Watchlist> all = watchlistManager->getAllWatchlists();
  for (const Watchlist &wl : all) {
    watchlistComboBox->addItem(wl.name, wl.id);
  }

  if (!all.isEmpty()) {
    if (currentWatchlistId.isEmpty() ||
        !watchlistManager->watchlistExists(currentWatchlistId)) {
      currentWatchlistId = all.first().id;
    }
    for (int i = 0; i < watchlistComboBox->count(); ++i) {
      if (watchlistComboBox->itemData(i).toString() == currentWatchlistId) {
        watchlistComboBox->setCurrentIndex(i);
        break;
      }
    }
  }

  watchlistComboBox->blockSignals(false);
  loadWatchlistItems();
}

void WatchlistWidget::onWatchlistSelected(int index) {
  if (index < 0)
    return;
  currentWatchlistId = watchlistComboBox->itemData(index).toString();
  selectedItemIndex = -1;
  loadWatchlistItems();
}

void WatchlistWidget::onCreateWatchlist() {
  bool ok;
  QString name = QInputDialog::getText(this, "New Watchlist",
                                        "Watchlist name:", QLineEdit::Normal,
                                        "", &ok);
  if (!ok || name.trimmed().isEmpty())
    return;

  QString id = watchlistManager->createWatchlist(name.trimmed());
  currentWatchlistId = id;
  refreshWatchlistList();
  emit watchlistUpdated();
}

void WatchlistWidget::onDeleteWatchlist() {
  if (currentWatchlistId.isEmpty())
    return;

  auto reply = QMessageBox::question(
      this, "Delete Watchlist", "Delete this watchlist and all its items?",
      QMessageBox::Yes | QMessageBox::No);
  if (reply != QMessageBox::Yes)
    return;

  watchlistManager->deleteWatchlist(currentWatchlistId);
  currentWatchlistId.clear();
  refreshWatchlistList();
  emit watchlistUpdated();
}

void WatchlistWidget::onRenameWatchlist() {
  if (currentWatchlistId.isEmpty())
    return;

  Watchlist wl = watchlistManager->getWatchlist(currentWatchlistId);
  bool ok;
  QString name = QInputDialog::getText(this, "Rename Watchlist", "New name:",
                                        QLineEdit::Normal, wl.name, &ok);
  if (!ok || name.trimmed().isEmpty())
    return;

  watchlistManager->renameWatchlist(currentWatchlistId, name.trimmed());
  refreshWatchlistList();
  emit watchlistUpdated();
}

// ---------------------------------------------------------------------------
// Item management
// ---------------------------------------------------------------------------
void WatchlistWidget::loadWatchlistItems() {
  populateTable();
  updateStats();
  if (selectedItemIndex >= 0)
    updateChart(selectedItemIndex);
}

void WatchlistWidget::populateTable() {
  watchlistTable->setSortingEnabled(false);
  watchlistTable->setRowCount(0);

  if (currentWatchlistId.isEmpty())
    return;

  Watchlist wl = watchlistManager->getWatchlist(currentWatchlistId);

  for (int i = 0; i < wl.items.size(); ++i) {
    const WatchlistItem &item = wl.items[i];
    int row = watchlistTable->rowCount();
    watchlistTable->insertRow(row);

    // Item Name
    auto *nameItem = new QTableWidgetItem(item.skinName);
    nameItem->setData(Qt::UserRole, i); // store index
    watchlistTable->setItem(row, 0, nameItem);

    // Condition
    watchlistTable->setItem(row, 1, new QTableWidgetItem(item.condition));

    // Current Price
    auto *priceItem = new WatchlistNumericItem(
        item.currentPrice > 0 ? QString("$%1").arg(item.currentPrice, 0, 'f', 2)
                               : "—");
    priceItem->setData(Qt::UserRole, item.currentPrice);
    priceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    watchlistTable->setItem(row, 2, priceItem);

    // 24h, 7d, 30d change columns
    struct ChangeCol {
      int col;
      qint64 secs;
    };
    QList<ChangeCol> changes = {
        {3, 86400}, {4, 7 * 86400}, {5, 30 * 86400}};
    for (const auto &c : changes) {
      QString text = calcChange(item, c.secs);
      double val = calcChangeValue(item, c.secs);
      auto *changeItem = new WatchlistNumericItem(text);
      changeItem->setData(Qt::UserRole, val);
      changeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      if (val > 0.001)
        changeItem->setForeground(QColor("#6ec66e"));
      else if (val < -0.001)
        changeItem->setForeground(QColor("#dc4646"));
      else
        changeItem->setForeground(QColor("#8b8fa3"));
      watchlistTable->setItem(row, c.col, changeItem);
    }
  }

  watchlistTable->setSortingEnabled(true);
}

QString WatchlistWidget::marketName(const WatchlistItem &item) const {
  return item.skinName + " (" + item.condition + ")";
}

QString WatchlistWidget::calcChange(const WatchlistItem &item,
                                    qint64 secondsAgo) const {
  if (item.priceHistory.size() < 2 || item.currentPrice <= 0)
    return QString::fromUtf8("\xe2\x80\x94");

  qint64 cutoff = QDateTime::currentSecsSinceEpoch() - secondsAgo;

  // Find closest point to cutoff
  double oldPrice = -1;
  qint64 bestDist = LLONG_MAX;
  for (const auto &pt : item.priceHistory) {
    qint64 dist = qAbs(pt.timestamp - cutoff);
    if (dist < bestDist) {
      bestDist = dist;
      oldPrice = pt.price;
    }
  }

  // Don't show change if the closest point is too far from cutoff
  // (more than 2x the period means no meaningful data)
  if (oldPrice <= 0 || bestDist > secondsAgo * 2)
    return QString::fromUtf8("\xe2\x80\x94");

  double pct = ((item.currentPrice - oldPrice) / oldPrice) * 100.0;
  QString sign = pct >= 0 ? "+" : "";
  return sign + QString::number(pct, 'f', 1) + "%";
}

double WatchlistWidget::calcChangeValue(const WatchlistItem &item,
                                        qint64 secondsAgo) const {
  if (item.priceHistory.size() < 2 || item.currentPrice <= 0)
    return 0.0;

  qint64 cutoff = QDateTime::currentSecsSinceEpoch() - secondsAgo;

  double oldPrice = -1;
  qint64 bestDist = LLONG_MAX;
  for (const auto &pt : item.priceHistory) {
    qint64 dist = qAbs(pt.timestamp - cutoff);
    if (dist < bestDist) {
      bestDist = dist;
      oldPrice = pt.price;
    }
  }

  if (oldPrice <= 0 || bestDist > secondsAgo * 2)
    return 0.0;

  return ((item.currentPrice - oldPrice) / oldPrice) * 100.0;
}

void WatchlistWidget::updateStats() {
  if (currentWatchlistId.isEmpty())
    return;

  Watchlist wl = watchlistManager->getWatchlist(currentWatchlistId);

  int count = wl.items.size();
  double totalVal = 0;
  double bestChange = 0;
  QString bestMover;

  for (const WatchlistItem &item : wl.items) {
    totalVal += item.currentPrice;
    double change24h = calcChangeValue(item, 86400);
    if (qAbs(change24h) > qAbs(bestChange)) {
      bestChange = change24h;
      bestMover = item.skinName;
    }
  }

  double avgPrice = count > 0 ? totalVal / count : 0;

  itemCountLabel->setText(QString::number(count));
  totalValueLabel->setText(QString("$%1").arg(totalVal, 0, 'f', 2));
  avgPriceLabel->setText(QString("$%1").arg(avgPrice, 0, 'f', 2));

  if (!bestMover.isEmpty() && qAbs(bestChange) > 0.001) {
    QString sign = bestChange >= 0 ? "+" : "";
    topMoverLabel->setText(
        bestMover.left(20) + " " + sign +
        QString::number(bestChange, 'f', 1) + "%");
    topMoverLabel->setStyleSheet(
        bestChange >= 0
            ? "color: #6ec66e; font-size: 14px; font-weight: bold;"
            : "color: #dc4646; font-size: 14px; font-weight: bold;");
  } else {
    topMoverLabel->setText(QString::fromUtf8("\xe2\x80\x94"));
    topMoverLabel->setStyleSheet(
        "color: #e0e0e0; font-size: 14px; font-weight: bold;");
  }
}

void WatchlistWidget::onAddItem() {
  QDialog dialog(this);
  dialog.setWindowTitle("Add to Watchlist");
  dialog.setMinimumWidth(400);
  QFormLayout form(&dialog);

  QComboBox weaponCombo;
  QStringList weapons = {"AK-47",
                         "M4A4",
                         "M4A1-S",
                         "AWP",
                         "FAMAS",
                         "Galil AR",
                         "AUG",
                         "SG 553",
                         "SSG 08",
                         "G3SG1",
                         "SCAR-20",
                         "Desert Eagle",
                         "USP-S",
                         "Glock-18",
                         "P2000",
                         "P250",
                         "Five-SeveN",
                         "CZ75-Auto",
                         "Tec-9",
                         "Dual Berettas",
                         "R8 Revolver",
                         "MP9",
                         "MAC-10",
                         "PP-Bizon",
                         "MP7",
                         "UMP-45",
                         "P90",
                         "MP5-SD",
                         "Nova",
                         "XM1014",
                         "Sawed-Off",
                         "MAG-7",
                         "M249",
                         "Negev",
                         "★ Karambit",
                         "★ M9 Bayonet",
                         "★ Bayonet",
                         "★ Butterfly Knife",
                         "★ Falchion Knife",
                         "★ Shadow Daggers",
                         "★ Gut Knife",
                         "★ Flip Knife",
                         "★ Navaja Knife",
                         "★ Stiletto Knife",
                         "★ Talon Knife",
                         "★ Ursus Knife",
                         "★ Huntsman Knife",
                         "★ Bowie Knife",
                         "★ Skeleton Knife",
                         "★ Kukri Knife",
                         "★ Sport Gloves",
                         "★ Specialist Gloves",
                         "★ Moto Gloves",
                         "★ Bloodhound Gloves",
                         "★ Hand Wraps",
                         "★ Hydra Gloves",
                         "★ Driver Gloves",
                         "★ Broken Fang Gloves"};
  weaponCombo.addItems(weapons);

  QLineEdit skinEdit;
  skinEdit.setPlaceholderText("e.g. Redline");

  QComboBox conditionCombo;
  conditionCombo.addItems({"Factory New", "Minimal Wear", "Field-Tested",
                           "Well-Worn", "Battle-Scarred"});

  form.addRow("Weapon:", &weaponCombo);
  form.addRow("Skin Name:", &skinEdit);
  form.addRow("Condition:", &conditionCombo);

  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  form.addRow(&buttons);
  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted)
    return;

  QString weapon = weaponCombo.currentText();
  QString skin = skinEdit.text().trimmed();
  QString condition = conditionCombo.currentText();

  if (skin.isEmpty()) {
    QMessageBox::warning(this, "Invalid", "Skin name cannot be empty.");
    return;
  }

  QString mktName = weapon + " | " + skin + " (" + condition + ")";

  WatchlistItem item;
  item.skinName = weapon + " | " + skin;
  item.condition = condition;
  item.addedDate = QDateTime::currentDateTime().toString(Qt::ISODate);

  double price = api->fetchPrice(mktName);
  item.currentPrice = price > 0.0 ? price : 0.0;

  // Record initial price point if we got a price
  if (item.currentPrice > 0) {
    WatchlistPricePoint pt;
    pt.timestamp = QDateTime::currentSecsSinceEpoch();
    pt.price = item.currentPrice;
    item.priceHistory.append(pt);
  }

  watchlistManager->addItem(currentWatchlistId, item);
  loadWatchlistItems();
  emit watchlistUpdated();
}

void WatchlistWidget::onRemoveItem() {
  int row = watchlistTable->currentRow();
  if (row < 0)
    return;

  QTableWidgetItem *nameCell = watchlistTable->item(row, 0);
  if (!nameCell)
    return;
  int dataIndex = nameCell->data(Qt::UserRole).toInt();

  auto reply =
      QMessageBox::question(this, "Confirm Remove",
                            "Remove this item from the watchlist?",
                            QMessageBox::Yes | QMessageBox::No);
  if (reply == QMessageBox::Yes) {
    watchlistManager->removeItem(currentWatchlistId, dataIndex);
    selectedItemIndex = -1;
    chartView->hide();
    chartPlaceholder->show();
    chartPlaceholder->setText("Select an item to view its price history.");
    chartItemLabel->hide();
    loadWatchlistItems();
    emit watchlistUpdated();
  }
}

void WatchlistWidget::onRefreshPrices() { updateAllPrices(); }

void WatchlistWidget::updateAllPrices() {
  if (currentWatchlistId.isEmpty())
    return;

  Watchlist wl = watchlistManager->getWatchlist(currentWatchlistId);
  for (int i = 0; i < wl.items.size(); ++i) {
    QString mktName = marketName(wl.items[i]);
    double price = api->fetchPrice(mktName);
    if (price > 0.0) {
      watchlistManager->updateItemPrice(currentWatchlistId, i, price);
    }
  }

  watchlistManager->recordPriceHistory(currentWatchlistId);
  loadWatchlistItems();
  emit watchlistUpdated();
}

// ---------------------------------------------------------------------------
// Chart
// ---------------------------------------------------------------------------
void WatchlistWidget::onItemClicked(int row, int /*column*/) {
  QTableWidgetItem *nameCell = watchlistTable->item(row, 0);
  if (!nameCell)
    return;
  selectedItemIndex = nameCell->data(Qt::UserRole).toInt();
  updateChart(selectedItemIndex);
}

void WatchlistWidget::updateChart(int itemIndex) {
  if (currentWatchlistId.isEmpty() || itemIndex < 0) {
    chartView->hide();
    chartPlaceholder->show();
    chartPlaceholder->setText("Select an item to view its price history.");
    chartItemLabel->hide();
    priceBadge->hide();
    return;
  }

  Watchlist wl = watchlistManager->getWatchlist(currentWatchlistId);
  if (itemIndex >= wl.items.size())
    return;

  const WatchlistItem &item = wl.items[itemIndex];

  // Filter by time range
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  qint64 cutoffMs = 0;
  if (selectedTimeRange == "24H")
    cutoffMs = now - qint64(86400) * 1000;
  else if (selectedTimeRange == "7D")
    cutoffMs = now - qint64(7 * 86400) * 1000;
  else if (selectedTimeRange == "1M")
    cutoffMs = now - qint64(30) * 86400 * 1000;
  else if (selectedTimeRange == "3M")
    cutoffMs = now - qint64(90) * 86400 * 1000;
  else if (selectedTimeRange == "6M")
    cutoffMs = now - qint64(180) * 86400 * 1000;
  else if (selectedTimeRange == "1Y")
    cutoffMs = now - qint64(365) * 86400 * 1000;

  QVector<WatchlistPricePoint> filtered;
  for (const auto &pt : item.priceHistory) {
    qint64 ptMs = pt.timestamp * 1000;
    if (cutoffMs == 0 || ptMs >= cutoffMs)
      filtered.append(pt);
  }

  chartItemLabel->setText(marketName(item) + " — Price History");
  chartItemLabel->show();

  if (filtered.size() < 2) {
    chartView->hide();
    chartPlaceholder->show();
    chartPlaceholder->setText(
        "Not enough data to display chart.\nAt least 2 history points are "
        "required.");
    priceBadge->hide();
    return;
  }

  chartPlaceholder->hide();
  chartView->show();

  priceSeries->clear();

  double minY = 1e18, maxY = -1e18;
  qint64 minX = LLONG_MAX, maxX = LLONG_MIN;

  for (const auto &pt : filtered) {
    qint64 ms = pt.timestamp * 1000;
    priceSeries->append(ms, pt.price);
    if (pt.price < minY)
      minY = pt.price;
    if (pt.price > maxY)
      maxY = pt.price;
    if (ms < minX)
      minX = ms;
    if (ms > maxX)
      maxX = ms;
  }

  double pad = (maxY - minY) * 0.1;
  if (pad < 0.01)
    pad = maxY * 0.1;

  axisX->setRange(QDateTime::fromMSecsSinceEpoch(minX),
                  QDateTime::fromMSecsSinceEpoch(maxX));
  axisY->setRange(minY - pad, maxY + pad);

  // Update badge
  double lastPrice = filtered.last().price;
  priceBadge->setText(QString("$%1").arg(lastPrice, 0, 'f', 2));
  priceBadge->adjustSize();
  priceBadge->move(chartView->width() - priceBadge->width() - 10, 20);
  priceBadge->show();

  // Color: green if price went up, red if down
  double firstPrice = filtered.first().price;
  QColor lineColor =
      lastPrice >= firstPrice ? QColor(0x6e, 0xc6, 0x6e) : QColor(0xdc, 0x46, 0x46);
  QPen pen(lineColor);
  pen.setWidth(2);
  priceSeries->setPen(pen);

  priceBadge->setStyleSheet(
      QString("background: %1; color: %2; font-size: 10px; font-weight: bold;"
              " padding: 2px 6px; border-radius: 3px;")
          .arg(lineColor.name(),
               lastPrice >= firstPrice ? "#0f1117" : "#fff"));
}

void WatchlistWidget::onTimeRangeChanged(const QString &range) {
  selectedTimeRange = range;
  for (auto *btn : timeRangeButtons)
    btn->setChecked(btn->text() == range);
  if (selectedItemIndex >= 0)
    updateChart(selectedItemIndex);
}

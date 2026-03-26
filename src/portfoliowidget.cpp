#include "portfoliowidget.h"
#include "portfoliomanager.h"
#include "priceempireapi.h"
#include "qrlogindialog.h"
#include "steamapi.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSplitter>
#include <QTextStream>
#include <QStyledItemDelegate>
#include <QThread>
#include <QTimeZone>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Float bar delegate — draws the float value (colored by wear tier) plus a
// thin progress bar at the bottom of the cell showing where the float sits
// within the skin's possible range.
// ---------------------------------------------------------------------------
class FloatBarDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  static QColor wearColor(double fv) {
    if (fv < 0.07)       return QColor("#4fc3f7");
    else if (fv < 0.15)  return QColor("#81c784");
    else if (fv < 0.38)  return QColor("#ffb74d");
    else if (fv < 0.45)  return QColor("#ef9a9a");
    else                 return QColor("#e57373");
  }

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    // Default rendering handles text and selection background
    QStyledItemDelegate::paint(painter, option, index);

    double fv   = index.data(Qt::UserRole).toDouble();
    double minF = index.data(Qt::UserRole + 1).toDouble();
    double maxF = index.data(Qt::UserRole + 2).toDouble();

    if (fv <= 0.0 || maxF <= minF)
      return;

    QRect r = option.rect;
    QRect barRect(r.left() + 3, r.bottom() - 5, r.width() - 6, 3);

    painter->fillRect(barRect, QColor(0x2a, 0x2d, 0x3a));

    double pos = qBound(0.0, (fv - minF) / (maxF - minF), 1.0);
    int fillW = qRound(barRect.width() * pos);
    if (fillW > 0)
      painter->fillRect(QRect(barRect.left(), barRect.top(), fillW, barRect.height()),
                        wearColor(fv));
  }

  QSize sizeHint(const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override {
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    return {s.width(), qMax(s.height(), 30)};
  }
};

// ---------------------------------------------------------------------------
// Helper: numeric sort for table columns that contain formatted numbers
// ---------------------------------------------------------------------------
class NumericTableWidgetItem : public QTableWidgetItem {
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
PortfolioWidget::PortfolioWidget(PriceEmpireAPI *api, SteamAPI *steamApi,
                                 PortfolioManager *portfolioManager,
                                 SteamCompanion *steamCompanion,
                                 ItemDatabase *itemDb,
                                 TradeHistoryManager *tradeHistoryManager,
                                 QWidget *parent)
    : QWidget(parent), api(api), steamApi(steamApi),
      portfolioManager(portfolioManager), itemDb(itemDb),
      tradeHistoryManager(tradeHistoryManager),
      autoSaveTimer(new QTimer(this)),
      loadingDialog(nullptr), steamCompanion(steamCompanion) {
  steamCompanion->blockSignals(true);
  setupUI();
  steamCompanion->blockSignals(false);

  // Legacy hidden widgets kept alive for slot compatibility
  steamIdEdit = new QLineEdit(this);
  steamLoginButton = new QPushButton(this);
  importSteamButton = new QPushButton(this);
  steamIdEdit->hide();
  steamLoginButton->hide();
  importSteamButton->hide();

  connect(steamApi, &SteamAPI::inventoryFetched, this,
          &PortfolioWidget::onSteamInventoryFetched);
  connect(steamApi, &SteamAPI::inventoryError, this,
          [this](const QString &error) {
            if (loadingDialog) {
              loadingDialog->close();
              loadingDialog->deleteLater();
              loadingDialog = nullptr;
            }
            QMessageBox::warning(this, "Steam Error", error);
            if (steamStatusLabel) {
              steamStatusLabel->setText("Error: " + error);
              steamStatusLabel->setStyleSheet("color: #DC4646;");
            }
          });

  connect(portfolioManager, &PortfolioManager::portfolioChanged, this,
          [this](const QString &id) {
            if (id == currentPortfolioId && portfolioTable)
              loadPortfolioItems();
          });

  refreshPortfolioList();

  connect(autoSaveTimer, &QTimer::timeout, portfolioManager,
          &PortfolioManager::saveToFile);
  autoSaveTimer->start(30000);

  // Steam throttled queue timer (2 s per request)
  priceCheckTimer = new QTimer(this);
  priceCheckTimer->setInterval(2000);
  connect(priceCheckTimer, &QTimer::timeout, this,
          &PortfolioWidget::onPriceCheckTick);

  // SteamAnalyst bulk result
  connect(api, &PriceEmpireAPI::pricesLoaded, this, [this]() {
    // Re-apply prices to the current portfolio whenever the bulk JSON refreshes
    updateAllPrices();
  });
}

PortfolioWidget::~PortfolioWidget() {
  if (loadingDialog) {
    loadingDialog->close();
    loadingDialog->deleteLater();
  }
}

// ---------------------------------------------------------------------------
// setupUI
// ---------------------------------------------------------------------------
void PortfolioWidget::setupUI() {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(6);
  mainLayout->setContentsMargins(8, 8, 8, 8);

  // Portfolio selector row
  QHBoxLayout *portfolioSelectLayout = new QHBoxLayout();
  portfolioSelectLayout->setSpacing(4);

  QLabel *portfolioLabel = new QLabel("Portfolio:", this);
  portfolioComboBox = new QComboBox(this);
  portfolioComboBox->setMinimumWidth(200);

  createPortfolioButton = new QPushButton("New", this);
  deletePortfolioButton = new QPushButton("Delete", this);
  renamePortfolioButton = new QPushButton("Rename", this);

  for (auto *btn :
       {createPortfolioButton, renamePortfolioButton, deletePortfolioButton})
    btn->setFixedHeight(26);

  portfolioSelectLayout->addWidget(portfolioLabel);
  portfolioSelectLayout->addWidget(portfolioComboBox, 1);
  portfolioSelectLayout->addWidget(createPortfolioButton);
  portfolioSelectLayout->addWidget(renamePortfolioButton);
  portfolioSelectLayout->addWidget(deletePortfolioButton);
  mainLayout->addLayout(portfolioSelectLayout);

  // Stats bar
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
    QLabel *valueLbl = new QLabel(QString::fromUtf8("\xe2\x80\x94"), statsBar);
    valueLbl->setStyleSheet(
        "color: #e0e0e0; font-size: 14px; font-weight: bold;");
    col->addWidget(titleLbl);
    col->addWidget(valueLbl);
    statsLayout->addLayout(col);
    return valueLbl;
  };

  totalInvestmentLabel = makeStatPair("INVESTED");
  currentValueLabel = makeStatPair("VALUE");
  totalProfitLabel = makeStatPair("PROFIT / LOSS");
  roiLabel = makeStatPair("ROI");
  statsLayout->addStretch();
  mainLayout->addWidget(statsBar);

  // Portfolio table
  portfolioTable = new QTableWidget(this);
  portfolioTable->setColumnCount(8);
  portfolioTable->setHorizontalHeaderLabels({"Skin Name", "Condition", "Float",
                                             "Qty", "Buy Price", "Current",
                                             "Profit/Loss", "ROI %"});
  portfolioTable->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Interactive);
  portfolioTable->horizontalHeader()->setStretchLastSection(true);
  portfolioTable->horizontalHeader()->setMinimumSectionSize(60);
  portfolioTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  portfolioTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  portfolioTable->setSortingEnabled(true);
  portfolioTable->setMinimumHeight(180);
  portfolioTable->setItemDelegateForColumn(2, new FloatBarDelegate(portfolioTable));
  connect(portfolioTable, &QTableWidget::itemDoubleClicked, this,
          [this]() { onEditItem(); });
  mainLayout->addWidget(portfolioTable, 1);

  // Table action buttons
  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(4);

  addButton = new QPushButton("Add Item", this);
  removeButton = new QPushButton("Remove", this);
  editButton = new QPushButton("Edit", this);
  exportButton = new QPushButton("Export CSV", this);
  importButton = new QPushButton("Import CSV", this);
  refreshButton = new QPushButton("Refresh Prices", this);

  removeButton->setEnabled(false);
  editButton->setEnabled(false);

  for (auto *btn : {addButton, removeButton, editButton, exportButton,
                    importButton, refreshButton}) {
    btn->setFixedHeight(28);
    buttonLayout->addWidget(btn);
  }
  mainLayout->addLayout(buttonLayout);

  // Steam / GC import group
  QGroupBox *steamGroup = new QGroupBox("Import from Steam", this);
  QVBoxLayout *steamLayout = new QVBoxLayout(steamGroup);

  steamStatusLabel =
      new QLabel("Use the buttons below to import your inventory.", steamGroup);
  gcStatusLabel = steamStatusLabel;
  steamStatusLabel->setStyleSheet("color: #aaa; font-size: 11px;");
  steamLayout->addWidget(steamStatusLabel);

  QHBoxLayout *gcButtonLayout = new QHBoxLayout();
  gcLoginButton = new QPushButton("Fetch Inventory", steamGroup);
  gcShowImportDialogButton =
      new QPushButton("Select Items to Import", steamGroup);
  gcShowImportDialogButton->setEnabled(false);

  gcLoginButton->setFixedHeight(28);
  gcShowImportDialogButton->setFixedHeight(28);

  gcButtonLayout->addWidget(gcLoginButton);
  gcButtonLayout->addWidget(gcShowImportDialogButton);
  gcButtonLayout->addStretch();
  steamLayout->addLayout(gcButtonLayout);

  QHBoxLayout *storageLayout = new QHBoxLayout();
  gcStorageUnitCombo = new QComboBox(steamGroup);
  gcStorageUnitButton = new QPushButton("Load Storage Unit", steamGroup);
  gcStorageUnitCombo->setEnabled(false);
  gcStorageUnitButton->setEnabled(false);
  gcStorageUnitButton->setFixedHeight(24);
  gcStorageUnitCombo->hide();
  gcStorageUnitButton->hide();
  storageLayout->addWidget(gcStorageUnitCombo, 1);
  storageLayout->addWidget(gcStorageUnitButton);
  steamLayout->addLayout(storageLayout);

  mainLayout->addWidget(steamGroup);

  // ── Chart section ────────────────────────────────────────────────────────
  chartContainer = new QWidget(this);
  QVBoxLayout *chartContainerLayout = new QVBoxLayout(chartContainer);
  chartContainerLayout->setContentsMargins(0, 4, 0, 0);
  chartContainerLayout->setSpacing(6);

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
    connect(btn, &QPushButton::clicked, this, [this, r]() {
      onTimeRangeChanged(r);
    });
    timeRangeLayout->addWidget(btn);
    timeRangeButtons.append(btn);
  }
  timeRangeLayout->addStretch();
  chartContainerLayout->addLayout(timeRangeLayout);

  // Performance stats row
  QHBoxLayout *perfLayout = new QHBoxLayout();
  perfLayout->setSpacing(16);
  perfLayout->setContentsMargins(0, 0, 0, 0);
  auto makePerfLabel = [&](const QString &period) -> QLabel * {
    auto *lbl = new QLabel(period + " —", chartContainer);
    lbl->setStyleSheet("color: #8b8fa3; font-size: 11px;");
    perfLayout->addWidget(lbl);
    return lbl;
  };
  perf24hLabel = makePerfLabel("24H");
  perf7dLabel = makePerfLabel("7D");
  perf30dLabel = makePerfLabel("30D");
  perf90dLabel = makePerfLabel("90D");
  perfLayout->addStretch();
  chartContainerLayout->addLayout(perfLayout);

  // Chart
  portfolioChart = new QChart();
  portfolioChart->setTitle("");
  portfolioChart->setAnimationOptions(QChart::NoAnimation);
  portfolioChart->legend()->hide();
  portfolioChart->setBackgroundBrush(QBrush(QColor(0x0f, 0x11, 0x17)));
  portfolioChart->setBackgroundPen(Qt::NoPen);
  portfolioChart->setPlotAreaBackgroundBrush(QBrush(QColor(0x0f, 0x11, 0x17)));
  portfolioChart->setPlotAreaBackgroundVisible(true);
  portfolioChart->setMargins(QMargins(0, 8, 0, 0));

  // Value series — cyan
  valueSeries = new QLineSeries();
  QPen valuePen(QColor(0x4f, 0xc3, 0xf7));
  valuePen.setWidth(2);
  valueSeries->setPen(valuePen);

  // Cost series — red dashed
  costSeries = new QLineSeries();
  QPen costPen(QColor(0xdc, 0x46, 0x46));
  costPen.setWidth(2);
  costPen.setStyle(Qt::DashLine);
  costSeries->setPen(costPen);

  portfolioChart->addSeries(valueSeries);
  portfolioChart->addSeries(costSeries);

  // X axis (bottom)
  axisX = new QDateTimeAxis();
  axisX->setTickCount(6);
  axisX->setFormat("MMM d");
  axisX->setTitleText("");
  axisX->setLabelsFont(QFont("Segoe UI", 8));
  axisX->setLabelsColor(QColor(0x64, 0x6e, 0x87));
  axisX->setLinePen(QPen(QColor(0x1e, 0x22, 0x33)));
  axisX->setGridLineVisible(false);
  axisX->setShadesVisible(false);
  portfolioChart->addAxis(axisX, Qt::AlignBottom);
  valueSeries->attachAxis(axisX);
  costSeries->attachAxis(axisX);

  // Y axis (RIGHT side)
  axisY = new QValueAxis();
  axisY->setLabelFormat("$%.0f");
  axisY->setTitleText("");
  axisY->setLabelsFont(QFont("Segoe UI", 8));
  axisY->setLabelsColor(QColor(0x64, 0x6e, 0x87));
  axisY->setLinePen(QPen(QColor(0x1e, 0x22, 0x33)));
  axisY->setGridLineColor(QColor(0x1e, 0x22, 0x33));
  axisY->setMinorGridLineVisible(false);
  axisY->setShadesVisible(false);
  portfolioChart->addAxis(axisY, Qt::AlignRight);
  valueSeries->attachAxis(axisY);
  costSeries->attachAxis(axisY);

  chartView = new QChartView(portfolioChart);
  chartView->setRenderHint(QPainter::Antialiasing);
  chartView->setMinimumHeight(300);
  chartView->setStyleSheet("background: #0f1117; border: none;");

  // Badge labels (value + cost) overlaid on chart
  valueBadge = new QLabel(chartView);
  valueBadge->setStyleSheet(
      "background: #4fc3f7; color: #0f1117; font-size: 10px; font-weight: "
      "bold;"
      " padding: 2px 6px; border-radius: 3px;");
  valueBadge->setFixedHeight(18);
  valueBadge->hide();

  costBadge = new QLabel(chartView);
  costBadge->setStyleSheet(
      "background: #dc4646; color: #fff; font-size: 10px; font-weight: bold;"
      " padding: 2px 6px; border-radius: 3px;");
  costBadge->setFixedHeight(18);
  costBadge->hide();

  // Placeholder for insufficient data
  chartPlaceholder = new QLabel("Not enough data to display chart.\nAt least 2 "
                                "history points are required.",
                                chartContainer);
  chartPlaceholder->setAlignment(Qt::AlignCenter);
  chartPlaceholder->setStyleSheet(
      "color: #555; font-size: 12px; padding: 40px;");
  chartPlaceholder->hide();

  chartContainerLayout->addWidget(chartView);
  chartContainerLayout->addWidget(chartPlaceholder);
  mainLayout->addWidget(chartContainer);

  updateChart();

  // Price-check progress bar (hidden until active)
  queueGroup = new QWidget(this);
  QHBoxLayout *queueLayout = new QHBoxLayout(queueGroup);
  queueLayout->setContentsMargins(4, 2, 4, 2);

  queueStatusLabel = new QLabel("No price checks pending.", queueGroup);
  queuePauseButton = new QPushButton("Pause", queueGroup);
  queuePauseButton->setEnabled(false);

  queueLayout->addWidget(queueStatusLabel, 1);
  queueLayout->addWidget(queuePauseButton);
  queueGroup->hide();
  mainLayout->addWidget(queueGroup);

  // Steam companion signals
  connect(steamCompanion, &SteamCompanion::qrCodeReady, this,
          [this](const QString &url) {
            QRLoginDialog *dlg = new QRLoginDialog(this);
            dlg->setQRUrl(url);
            connect(steamCompanion, &SteamCompanion::qrScanned, dlg, [dlg]() {
              dlg->setStatus("QR scanned! Approve in Steam app...");
            });
            connect(steamCompanion, &SteamCompanion::loggedIn, dlg,
                    [dlg](const QString &) { dlg->markSuccess(); });
            dlg->exec();
            dlg->deleteLater();
          });

  connect(steamCompanion, &SteamCompanion::loggedIn, this,
          [this](const QString &steamId) {
            if (gcStatusLabel) {
              gcStatusLabel->setText(QString("Logged in (%1)").arg(steamId));
              gcStatusLabel->setStyleSheet("color: green; font-size: 11px;");
            }
          });

  connect(steamCompanion, &SteamCompanion::gcReady, this, [this]() {
    if (gcImportButton) {
      gcImportButton->setEnabled(true);
      gcImportButton->show();
    }
    if (gcStatusLabel)
      gcStatusLabel->setText("Connected to CS2 GC -- ready to import.");
  });

  connect(steamCompanion, &SteamCompanion::statusMessage, this,
          [this](const QString &msg) {
            if (gcStatusLabel)
              gcStatusLabel->setText(msg);
          });

  connect(steamCompanion, &SteamCompanion::errorOccurred, this,
          [this](const QString &err) {
            if (gcStatusLabel) {
              gcStatusLabel->setText("Error: " + err);
              gcStatusLabel->setStyleSheet("color: red; font-size: 11px;");
            }
            if (gcLoginButton)
              gcLoginButton->setEnabled(true);
          });

  connect(steamCompanion, &SteamCompanion::inventoryReceived, this,
          &PortfolioWidget::onGCInventoryReceived);
  connect(steamCompanion, &SteamCompanion::storageUnitReceived, this,
          &PortfolioWidget::onGCStorageUnitReceived);
  connect(steamCompanion, &SteamCompanion::floatsReceived, this,
          &PortfolioWidget::onFloatsReceived);

  connect(gcLoginButton, &QPushButton::clicked, this,
          &PortfolioWidget::onGCLoginClicked);
  if (gcImportButton)
    connect(gcImportButton, &QPushButton::clicked, this,
            &PortfolioWidget::onGCImportClicked);
  connect(gcStorageUnitButton, &QPushButton::clicked, this,
          &PortfolioWidget::onGCStorageUnitClicked);

  connect(gcShowImportDialogButton, &QPushButton::clicked, this, [this]() {
    if (lastFetchedItems.isEmpty())
      return;
    QVector<SteamInventoryItem> steamItems;
    for (const GCItem &gcItem : lastFetchedItems) {
      SteamInventoryItem si;
      si.marketHashName =
          gcItem.marketHashName.isEmpty() ? gcItem.name : gcItem.marketHashName;
      si.exterior = gcItem.exterior;
      si.iconUrl = gcItem.iconUrl;
      si.tradable = gcItem.tradable;
      si.type = QString::number(gcItem.defIndex);
      si.rarity = QString::number(gcItem.rarity);
      steamItems.append(si);
    }
    showImportDialog(steamItems);
  });

  // Pause/resume Steam queue
  connect(queuePauseButton, &QPushButton::clicked, this, [this]() {
    priceCheckPaused = !priceCheckPaused;
    if (priceCheckPaused) {
      priceCheckTimer->stop();
      queuePauseButton->setText("Resume");
      queueStatusLabel->setText(queueStatusLabel->text() + " (Paused)");
    } else {
      priceCheckTimer->start();
      queuePauseButton->setText("Pause");
      updateQueueStatusLabel();
    }
  });

  connect(portfolioComboBox,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PortfolioWidget::onPortfolioSelected);
  connect(createPortfolioButton, &QPushButton::clicked, this,
          &PortfolioWidget::onCreatePortfolio);
  connect(deletePortfolioButton, &QPushButton::clicked, this,
          &PortfolioWidget::onDeletePortfolio);
  connect(renamePortfolioButton, &QPushButton::clicked, this,
          &PortfolioWidget::onRenamePortfolio);
  connect(addButton, &QPushButton::clicked, this, &PortfolioWidget::onAddItem);
  connect(removeButton, &QPushButton::clicked, this,
          &PortfolioWidget::onRemoveItem);
  connect(editButton, &QPushButton::clicked, this,
          &PortfolioWidget::onEditItem);
  connect(exportButton, &QPushButton::clicked, this,
          &PortfolioWidget::onExportCSV);
  connect(importButton, &QPushButton::clicked, this,
          &PortfolioWidget::onImportCSV);
  connect(refreshButton, &QPushButton::clicked, this,
          &PortfolioWidget::onRefreshPrices);

  connect(portfolioTable, &QTableWidget::itemSelectionChanged, this, [this]() {
    if (removeButton && editButton) {
      bool sel = portfolioTable->currentRow() >= 0;
      removeButton->setEnabled(sel);
      editButton->setEnabled(sel);
    }
  });

  if (steamCompanion && steamCompanion->isGCReady()) {
    if (gcStatusLabel)
      gcStatusLabel->setText("Connected to CS2 GC — ready to import.");
    if (gcLoginButton)
      gcLoginButton->setText("Import from Steam");
    if (gcImportButton)
      gcImportButton->setEnabled(true);
  }
}

// ---------------------------------------------------------------------------
// Portfolio management
// ---------------------------------------------------------------------------

void PortfolioWidget::refreshPortfolioList() {
  portfolioComboBox->clear();
  QVector<Portfolio> portfolios = portfolioManager->getAllPortfolios();
  for (const Portfolio &p : portfolios)
    portfolioComboBox->addItem(p.name, p.id);

  if (!portfolios.isEmpty()) {
    currentPortfolioId = portfolios.first().id;
    loadPortfolioItems();
    Portfolio p = portfolioManager->getPortfolio(currentPortfolioId);
    if (!p.items.isEmpty() && p.history.isEmpty())
      portfolioManager->recordHistoryPoint(currentPortfolioId);
  }
}

void PortfolioWidget::loadPortfolioItems() {
  populateTable();
  calculateStatistics();
  updateChart();
}

void PortfolioWidget::populateTable() {
  if (!portfolioTable)
    return;
  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);

  portfolioTable->setSortingEnabled(false);
  portfolioTable->setRowCount(portfolio.items.size());

  for (int i = 0; i < portfolio.items.size(); ++i) {
    const PortfolioItem &item = portfolio.items[i];

    double profit = (item.currentPrice - item.buyPrice) * item.quantity;
    double roi =
        item.buyPrice > 0
            ? ((item.currentPrice - item.buyPrice) / item.buyPrice) * 100.0
            : 0.0;

    QTableWidgetItem *nameItem = new QTableWidgetItem(item.skinName);
    nameItem->setData(Qt::UserRole, i);

    QTableWidgetItem *conditionItem = new QTableWidgetItem(item.condition);

    double fv = item.floatValue;
    auto *floatItem = new NumericTableWidgetItem(fv > 0.0 ? QString::number(fv, 'f', 6) : "");
    floatItem->setData(Qt::UserRole, fv);
    if (fv > 0.0) {
      floatItem->setForeground(FloatBarDelegate::wearColor(fv));
      double minF = 0.0, maxF = 1.0;
      if (itemDb && itemDb->isLoaded()) {
        ItemInfo info = itemDb->lookup(marketName(item));
        minF = info.minFloat;
        maxF = info.maxFloat;
      }
      floatItem->setData(Qt::UserRole + 1, minF);
      floatItem->setData(Qt::UserRole + 2, maxF);
    }

    auto *qtyItem = new NumericTableWidgetItem(QString::number(item.quantity));
    qtyItem->setData(Qt::UserRole, item.quantity);

    auto *buyItem = new NumericTableWidgetItem(
        QString("$%1").arg(item.buyPrice, 0, 'f', 2));
    buyItem->setData(Qt::UserRole, item.buyPrice);

    auto *currentItem = new NumericTableWidgetItem(
        QString("$%1").arg(item.currentPrice, 0, 'f', 2));
    currentItem->setData(Qt::UserRole, item.currentPrice);

    auto *profitItem =
        new NumericTableWidgetItem(QString("$%1").arg(profit, 0, 'f', 2));
    profitItem->setData(Qt::UserRole, profit);
    profitItem->setForeground(profit >= 0 ? QColor(0, 150, 0)
                                          : QColor(200, 0, 0));

    auto *roiItem =
        new NumericTableWidgetItem(QString("%1%").arg(roi, 0, 'f', 2));
    roiItem->setData(Qt::UserRole, roi);
    roiItem->setForeground(roi >= 0 ? QColor(0, 150, 0) : QColor(200, 0, 0));

    portfolioTable->setItem(i, 0, nameItem);
    portfolioTable->setItem(i, 1, conditionItem);
    portfolioTable->setItem(i, 2, floatItem);
    portfolioTable->setItem(i, 3, qtyItem);
    portfolioTable->setItem(i, 4, buyItem);
    portfolioTable->setItem(i, 5, currentItem);
    portfolioTable->setItem(i, 6, profitItem);
    portfolioTable->setItem(i, 7, roiItem);
  }

  portfolioTable->setSortingEnabled(true);
}

void PortfolioWidget::calculateStatistics() {
  if (!totalInvestmentLabel || !currentValueLabel || !totalProfitLabel ||
      !roiLabel)
    return;

  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);
  double totalInvestment = 0.0, currentValue = 0.0;

  for (const PortfolioItem &item : portfolio.items) {
    totalInvestment += item.buyPrice * item.quantity;
    currentValue += item.currentPrice * item.quantity;
  }

  double profit = currentValue - totalInvestment;
  double roi = totalInvestment > 0 ? (profit / totalInvestment) * 100.0 : 0.0;

  totalInvestmentLabel->setText(QString("$%1").arg(totalInvestment, 0, 'f', 2));
  currentValueLabel->setText(QString("$%1").arg(currentValue, 0, 'f', 2));

  totalProfitLabel->setText(QString("$%1").arg(profit, 0, 'f', 2));
  totalProfitLabel->setStyleSheet(
      QString("font-size: 14px; font-weight: bold; color: %1;")
          .arg(profit >= 0 ? "#28c878" : "#dc4646"));

  roiLabel->setText(QString("%1%").arg(roi, 0, 'f', 2));
  roiLabel->setStyleSheet(
      QString("font-size: 14px; font-weight: bold; color: %1;")
          .arg(roi >= 0 ? "#28c878" : "#dc4646"));
}

void PortfolioWidget::onTimeRangeChanged(const QString &range) {
  selectedTimeRange = range;
  for (auto *btn : timeRangeButtons)
    btn->setChecked(btn->text() == range);
  updateChart();
}

void PortfolioWidget::updateChart() {
  valueSeries->clear();
  costSeries->clear();
  valueBadge->hide();
  costBadge->hide();

  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);

  // Filter history by selected time range
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  qint64 cutoff = 0;
  if (selectedTimeRange == "24H")
    cutoff = now - qint64(24) * 3600 * 1000;
  else if (selectedTimeRange == "7D")
    cutoff = now - qint64(7) * 24 * 3600 * 1000;
  else if (selectedTimeRange == "1M")
    cutoff = now - qint64(30) * 24 * 3600 * 1000;
  else if (selectedTimeRange == "3M")
    cutoff = now - qint64(90) * 24 * 3600 * 1000;
  else if (selectedTimeRange == "6M")
    cutoff = now - qint64(180) * 24 * 3600 * 1000;
  else if (selectedTimeRange == "1Y")
    cutoff = now - qint64(365) * 24 * 3600 * 1000;

  QList<PortfolioHistoryPoint> filtered;
  for (const auto &pt : portfolio.history) {
    if (pt.timestamp >= cutoff)
      filtered.append(pt);
  }

  // Placeholder if < 2 points — on first launch synthesise a seed point
  if (filtered.size() < 2) {
    if (portfolio.history.size() < 2) {
      // No real history yet: compute current totals and fake a 24-hour window
      double totalValue = 0.0, totalCost = 0.0;
      for (const auto &item : portfolio.items) {
        totalValue += item.currentPrice * item.quantity;
        totalCost  += item.buyPrice     * item.quantity;
      }
      if (totalValue > 0.0 || totalCost > 0.0) {
        filtered.clear();
        PortfolioHistoryPoint seed, cur;
        seed.timestamp  = now - qint64(24) * 3600 * 1000;
        seed.totalValue = totalValue;
        seed.totalCost  = totalCost;
        cur.timestamp   = now;
        cur.totalValue  = totalValue;
        cur.totalCost   = totalCost;
        filtered.append(seed);
        filtered.append(cur);
      } else {
        chartView->hide();
        chartPlaceholder->show();
        return;
      }
    } else {
      // Real history exists but none falls in the selected time range
      chartView->hide();
      chartPlaceholder->show();
      return;
    }
  }
  chartPlaceholder->hide();
  chartView->show();

  double minV = std::numeric_limits<double>::max();
  double maxV = std::numeric_limits<double>::lowest();

  for (const auto &pt : filtered) {
    valueSeries->append(pt.timestamp, pt.totalValue);
    costSeries->append(pt.timestamp, pt.totalCost);
    minV = std::min(minV, std::min(pt.totalValue, pt.totalCost));
    maxV = std::max(maxV, std::max(pt.totalValue, pt.totalCost));
  }

  axisX->setRange(
      QDateTime::fromMSecsSinceEpoch(filtered.first().timestamp, QTimeZone::UTC),
      QDateTime::fromMSecsSinceEpoch(filtered.last().timestamp, QTimeZone::UTC));

  double padding = (maxV - minV) * 0.1;
  if (padding < 1.0)
    padding = 1.0;
  axisY->setRange(std::max(0.0, minV - padding), maxV + padding);

  // Update value series color — green if profit, red if loss
  double lastValue = filtered.last().totalValue;
  double lastCost = filtered.last().totalCost;
  bool profitable = lastValue >= lastCost;
  QPen vPen(profitable ? QColor(0x38, 0xc8, 0x78) : QColor(0xdc, 0x46, 0x46));
  vPen.setWidth(2);
  valueSeries->setPen(vPen);

  // Badge labels
  valueBadge->setText(QString("Value $%1").arg(lastValue, 0, 'f', 0));
  valueBadge->adjustSize();
  valueBadge->setStyleSheet(
      QString("background: %1; color: %2; font-size: 10px; font-weight: bold;"
              " padding: 2px 6px; border-radius: 3px;")
          .arg(profitable ? "#38c878" : "#dc4646",
               profitable ? "#0f1117" : "#fff"));
  valueBadge->show();

  costBadge->setText(QString("Cost $%1").arg(lastCost, 0, 'f', 0));
  costBadge->adjustSize();
  costBadge->show();

  // Position badges at right edge of chart view
  int badgeX = chartView->width() - valueBadge->width() - 12;
  valueBadge->move(std::max(4, badgeX), 20);
  costBadge->move(std::max(4, badgeX), 42);

  // Update performance stats
  auto calcPerf = [&](qint64 periodMs) -> QString {
    qint64 periodCutoff = now - periodMs;
    // Find earliest point in range
    double startVal = -1;
    for (const auto &pt : portfolio.history) {
      if (pt.timestamp >= periodCutoff) {
        startVal = pt.totalValue;
        break;
      }
    }
    if (startVal <= 0)
      return "—";
    double pct = ((lastValue - startVal) / startVal) * 100.0;
    return QString("%1%2%")
        .arg(pct >= 0 ? "+" : "")
        .arg(pct, 0, 'f', 1);
  };

  auto setPerfStyle = [](QLabel *lbl, const QString &period,
                         const QString &val) {
    bool positive = val.startsWith('+');
    bool negative = val.startsWith('-');
    QString color = positive ? "#38c878" : (negative ? "#dc4646" : "#8b8fa3");
    lbl->setText(QString("<span style='color:#8b8fa3;'>%1 </span>"
                         "<span style='color:%2;'>%3</span>")
                     .arg(period, color, val));
  };

  setPerfStyle(perf24hLabel, "24H", calcPerf(qint64(24) * 3600 * 1000));
  setPerfStyle(perf7dLabel, "7D", calcPerf(qint64(7) * 24 * 3600 * 1000));
  setPerfStyle(perf30dLabel, "30D", calcPerf(qint64(30) * 24 * 3600 * 1000));
  setPerfStyle(perf90dLabel, "90D", calcPerf(qint64(90) * 24 * 3600 * 1000));
}

// ---------------------------------------------------------------------------
// Price refresh — branches on the active source
// ---------------------------------------------------------------------------

void PortfolioWidget::onRefreshPrices() { updateAllPrices(); }

void PortfolioWidget::updateAllPrices() {
  if (!api->arePricesLoaded())
    return;

  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);
  if (portfolio.items.isEmpty())
    return;

  int updated = 0;
  for (int i = 0; i < portfolio.items.size(); ++i) {
    PortfolioItem item = portfolio.items[i];
    double price = api->fetchPrice(marketName(item));
    if (price > 0.0) {
      item.currentPrice = price;
      portfolioManager->updateItem(currentPortfolioId, i, item);
      ++updated;
    }
  }

  loadPortfolioItems();
  calculateStatistics();
  portfolioManager->recordHistoryPoint(currentPortfolioId,
                                       api->isFastSource());
  updateChart();
  emit portfolioUpdated();
}

// ---------------------------------------------------------------------------
// Steam queue (throttled path)
// ---------------------------------------------------------------------------

void PortfolioWidget::enqueuePriceCheck(const QString &portfolioId,
                                        int itemIndex,
                                        const QString &marketName) {
  PriceCheckJob job;
  job.portfolioId = portfolioId;
  job.itemIndex = itemIndex;
  job.marketName = marketName;
  priceCheckQueue.enqueue(job);

  queueGroup->show();
  updateQueueStatusLabel();
  queuePauseButton->setEnabled(true);

  if (!priceCheckTimer->isActive() && !priceCheckPaused)
    priceCheckTimer->start();
}

void PortfolioWidget::onPriceCheckTick() {
  if (priceCheckQueue.isEmpty()) {
    priceCheckTimer->stop();
    queuePauseButton->setEnabled(false);
    queuePauseButton->setText("Pause");
    priceCheckPaused = false;
    queueStatusLabel->setText(
        QString("Done — %1 items price checked.").arg(priceCheckDone));
    loadPortfolioItems();
    calculateStatistics();
    portfolioManager->recordHistoryPoint(currentPortfolioId,
                                         api->isFastSource());
    updateChart();
    QTimer::singleShot(3000, this, [this]() { queueGroup->hide(); });
    return;
  }

  PriceCheckJob job = priceCheckQueue.dequeue();
  priceCheckDone++;
  updateQueueStatusLabel();

  double price = api->fetchPrice(job.marketName);
  if (price > 0) {
    Portfolio portfolio = portfolioManager->getPortfolio(job.portfolioId);
    if (job.itemIndex < portfolio.items.size()) {
      PortfolioItem item = portfolio.items[job.itemIndex];
      item.currentPrice = price;
      portfolioManager->updateItem(job.portfolioId, job.itemIndex, item);
    }
  }

  if (priceCheckDone % 5 == 0)
    loadPortfolioItems();
}

void PortfolioWidget::updateQueueStatusLabel() {
  int remaining = priceCheckQueue.size();
  if (remaining == 0)
    return;

  int secondsLeft = remaining * 2;
  QString timeStr =
      secondsLeft < 60
          ? QString("%1s").arg(secondsLeft)
          : QString("%1m %2s").arg(secondsLeft / 60).arg(secondsLeft % 60);

  queueStatusLabel->setText(QString("Steam: %1 done, %2 remaining (~%3 left)")
                                .arg(priceCheckDone)
                                .arg(remaining)
                                .arg(timeStr));
}

// ---------------------------------------------------------------------------
// Portfolio CRUD slots
// ---------------------------------------------------------------------------

void PortfolioWidget::onPortfolioSelected(int index) {
  if (index < 0)
    return;
  currentPortfolioId = portfolioComboBox->itemData(index).toString();
  loadPortfolioItems();
}

void PortfolioWidget::onCreatePortfolio() {
  QString name =
      QInputDialog::getText(this, "Create Portfolio", "Portfolio name:");
  if (!name.isEmpty()) {
    QString id = portfolioManager->createPortfolio(name);
    refreshPortfolioList();
    int idx = portfolioComboBox->findData(id);
    if (idx >= 0)
      portfolioComboBox->setCurrentIndex(idx);
  }
}

void PortfolioWidget::onDeletePortfolio() {
  if (currentPortfolioId.isEmpty())
    return;
  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);
  auto reply = QMessageBox::question(
      this, "Delete Portfolio",
      QString("Delete '%1' and all its items?").arg(portfolio.name),
      QMessageBox::Yes | QMessageBox::No);
  if (reply == QMessageBox::Yes) {
    portfolioManager->deletePortfolio(currentPortfolioId);
    refreshPortfolioList();
  }
}

void PortfolioWidget::onRenamePortfolio() {
  if (currentPortfolioId.isEmpty())
    return;
  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);
  QString newName = QInputDialog::getText(
      this, "Rename Portfolio", "New name:", QLineEdit::Normal, portfolio.name);
  if (!newName.isEmpty() && newName != portfolio.name) {
    portfolioManager->renamePortfolio(currentPortfolioId, newName);
    refreshPortfolioList();
  }
}

// ---------------------------------------------------------------------------
// Add / Remove / Edit item
// ---------------------------------------------------------------------------

void PortfolioWidget::onAddItem() {
  QDialog dialog(this);
  dialog.setWindowTitle("Add Item");
  dialog.setMinimumWidth(450);
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

  QDoubleSpinBox floatSpinBox;
  floatSpinBox.setRange(0.0, 1.0);
  floatSpinBox.setDecimals(6);
  floatSpinBox.setValue(0.15);

  QSpinBox quantitySpinBox;
  quantitySpinBox.setRange(1, 9999);
  quantitySpinBox.setValue(1);

  QDoubleSpinBox buyPriceSpinBox;
  buyPriceSpinBox.setRange(0.0, 999999.0);
  buyPriceSpinBox.setDecimals(2);
  buyPriceSpinBox.setPrefix("$");

  QLineEdit notesEdit;

  QCheckBox *logToHistory = nullptr;
  if (tradeHistoryManager) {
    logToHistory = new QCheckBox("Log to trade history", &dialog);
    logToHistory->setChecked(true);
  }

  form.addRow("Weapon:", &weaponCombo);
  form.addRow("Skin Name:", &skinEdit);
  form.addRow("Condition:", &conditionCombo);
  form.addRow("Float:", &floatSpinBox);
  form.addRow("Quantity:", &quantitySpinBox);
  form.addRow("Buy Price:", &buyPriceSpinBox);
  form.addRow("Notes:", &notesEdit);
  if (logToHistory)
    form.addRow("", logToHistory);

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

  QString marketName = weapon + " | " + skin + " (" + condition + ")";

  PortfolioItem item;
  item.skinName = weapon + " | " + skin;
  item.condition = condition;
  item.floatValue = floatSpinBox.value();
  item.quantity = quantitySpinBox.value();
  item.buyPrice = buyPriceSpinBox.value();
  item.currentPrice = 0;
  item.purchaseDate = QDate::currentDate().toString("yyyy-MM-dd");
  item.notes = notesEdit.text();

  double price = api->fetchPrice(marketName);
  item.currentPrice = price > 0.0 ? price : item.buyPrice;

  portfolioManager->addItem(currentPortfolioId, item);

  if (logToHistory && logToHistory->isChecked() && item.buyPrice > 0.0) {
    TradeHistoryEntry entry;
    entry.itemName = marketName;
    entry.type = "manual_buy";
    entry.price = item.buyPrice;
    entry.buyPrice = item.buyPrice;
    entry.quantity = item.quantity;
    entry.timestamp = QDateTime::currentMSecsSinceEpoch();
    entry.notes = item.notes;
    tradeHistoryManager->addEntry(entry);
  }

  loadPortfolioItems();
  emit portfolioUpdated();
}

void PortfolioWidget::onRemoveItem() {
  int row = portfolioTable->currentRow();
  if (row < 0)
    return;

  QTableWidgetItem *nameCell = portfolioTable->item(row, 0);
  if (!nameCell)
    return;
  int dataIndex = nameCell->data(Qt::UserRole).toInt();

  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);
  if (dataIndex < 0 || dataIndex >= portfolio.items.size())
    return;
  const PortfolioItem &cur = portfolio.items[dataIndex];

  QDialog dialog(this);
  dialog.setWindowTitle("Remove Item");
  dialog.setMinimumWidth(350);
  auto *form = new QFormLayout(&dialog);

  auto *infoLabel =
      new QLabel(QString("Remove \"%1\"?").arg(cur.skinName), &dialog);
  form->addRow(infoLabel);

  QDoubleSpinBox *sellPriceSpinBox = nullptr;
  QCheckBox *logToHistory = nullptr;

  if (tradeHistoryManager) {
    sellPriceSpinBox = new QDoubleSpinBox(&dialog);
    sellPriceSpinBox->setRange(0.0, 999999.0);
    sellPriceSpinBox->setDecimals(2);
    sellPriceSpinBox->setPrefix("$");
    sellPriceSpinBox->setValue(cur.currentPrice);

    logToHistory = new QCheckBox("Log to trade history", &dialog);
    logToHistory->setChecked(true);

    form->addRow("Sell Price:", sellPriceSpinBox);
    form->addRow("", logToHistory);
  }

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::Cancel);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted)
    return;

  if (logToHistory && logToHistory->isChecked() &&
      sellPriceSpinBox->value() > 0.0) {
    TradeHistoryEntry entry;
    entry.itemName = marketName(cur);
    entry.type = "manual_sell";
    entry.price = sellPriceSpinBox->value();
    entry.sellPrice = sellPriceSpinBox->value();
    entry.quantity = cur.quantity;
    entry.timestamp = QDateTime::currentMSecsSinceEpoch();
    tradeHistoryManager->addEntry(entry);
  }

  portfolioManager->removeItem(currentPortfolioId, dataIndex);
  loadPortfolioItems();
  emit portfolioUpdated();
}

void PortfolioWidget::onEditItem() {
  int row = portfolioTable->currentRow();
  if (row < 0)
    return;

  QTableWidgetItem *nameCell = portfolioTable->item(row, 0);
  if (!nameCell)
    return;
  int dataIndex = nameCell->data(Qt::UserRole).toInt();

  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);
  if (dataIndex >= portfolio.items.size())
    return;

  const PortfolioItem &cur = portfolio.items[dataIndex];

  QDialog dialog(this);
  dialog.setWindowTitle("Edit Item");
  dialog.setMinimumWidth(400);
  QFormLayout form(&dialog);

  QLineEdit skinNameEdit(cur.skinName);
  QComboBox conditionCombo;
  conditionCombo.addItems({"Factory New", "Minimal Wear", "Field-Tested",
                           "Well-Worn", "Battle-Scarred"});
  conditionCombo.setCurrentText(cur.condition);

  QDoubleSpinBox floatSpinBox;
  floatSpinBox.setRange(0.0, 1.0);
  floatSpinBox.setDecimals(6);
  floatSpinBox.setValue(cur.floatValue);

  QSpinBox quantitySpinBox;
  quantitySpinBox.setRange(1, 9999);
  quantitySpinBox.setValue(cur.quantity);

  QDoubleSpinBox buyPriceSpinBox;
  buyPriceSpinBox.setRange(0.0, 999999.0);
  buyPriceSpinBox.setDecimals(2);
  buyPriceSpinBox.setPrefix("$");
  buyPriceSpinBox.setValue(cur.buyPrice);

  QLineEdit notesEdit(cur.notes);

  form.addRow("Skin Name:", &skinNameEdit);
  form.addRow("Condition:", &conditionCombo);
  form.addRow("Float:", &floatSpinBox);
  form.addRow("Quantity:", &quantitySpinBox);
  form.addRow("Buy Price:", &buyPriceSpinBox);
  form.addRow("Notes:", &notesEdit);

  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  form.addRow(&buttons);
  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted)
    return;

  PortfolioItem updated = cur;
  updated.skinName = skinNameEdit.text().trimmed();
  updated.condition = conditionCombo.currentText();
  updated.floatValue = floatSpinBox.value();
  updated.quantity = quantitySpinBox.value();
  updated.buyPrice = buyPriceSpinBox.value();
  updated.notes = notesEdit.text();

  portfolioManager->updateItem(currentPortfolioId, dataIndex, updated);
  loadPortfolioItems();
  emit portfolioUpdated();
}

// ---------------------------------------------------------------------------
// CSV export / import
// ---------------------------------------------------------------------------

void PortfolioWidget::onExportCSV() {
  if (currentPortfolioId.isEmpty())
    return;

  QString fileName = QFileDialog::getSaveFileName(
      this, "Export Portfolio", "portfolio.csv", "CSV Files (*.csv)");
  if (fileName.isEmpty())
    return;

  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, "Error", "Could not open file for writing.");
    return;
  }

  QTextStream out(&file);
  out << "Skin Name,Condition,Float,Quantity,Buy Price,Current "
         "Price,Purchase "
         "Date,Notes\n";

  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);
  for (const PortfolioItem &item : portfolio.items) {
    out << QString("\"%1\",%2,%3,%4,%5,%6,\"%7\",\"%8\"\n")
               .arg(item.skinName)
               .arg(item.condition)
               .arg(item.floatValue)
               .arg(item.quantity)
               .arg(item.buyPrice)
               .arg(item.currentPrice)
               .arg(item.purchaseDate)
               .arg(item.notes);
  }

  file.close();
  QMessageBox::information(
      this, "Export Complete",
      QString("Exported %1 items.").arg(portfolio.items.size()));
}

void PortfolioWidget::onImportCSV() {
  if (currentPortfolioId.isEmpty())
    return;

  QString fileName =
      QFileDialog::getOpenFileName(this, "Import CSV", "", "CSV Files (*.csv)");
  if (fileName.isEmpty())
    return;

  QFile file(fileName);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::warning(this, "Error", "Could not open file.");
    return;
  }

  QTextStream in(&file);
  in.readLine(); // skip header

  QVector<PortfolioItem> importItems;

  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    if (line.isEmpty())
      continue;

    QStringList parts;
    QString current;
    bool inQuotes = false;
    for (QChar c : line) {
      if (c == '"') {
        inQuotes = !inQuotes;
      } else if (c == ',' && !inQuotes) {
        parts.append(current.trimmed());
        current.clear();
      } else {
        current.append(c);
      }
    }
    parts.append(current.trimmed());

    if (parts.size() >= 7) {
      PortfolioItem item;
      item.skinName = parts[0].remove('"');
      item.condition = parts[1];
      item.floatValue = parts[2].toDouble();
      item.quantity = qMax(1, parts[3].toInt());
      item.buyPrice = parts[4].toDouble();
      item.currentPrice = parts[5].toDouble();
      item.purchaseDate = parts[6].remove('"');
      if (parts.size() >= 8)
        item.notes = parts[7].remove('"');
      if (!item.skinName.isEmpty())
        importItems.append(item);
    }
  }
  file.close();

  if (importItems.isEmpty()) {
    QMessageBox::warning(this, "Import Failed", "No valid items found in CSV.");
    return;
  }

  portfolioManager->importFromSteamInventory(currentPortfolioId, importItems);
  loadPortfolioItems();
  QMessageBox::information(
      this, "Import Complete",
      QString("Imported %1 items.").arg(importItems.size()));
  emit portfolioUpdated();
}

// ---------------------------------------------------------------------------
// Steam inventory import
// ---------------------------------------------------------------------------

void PortfolioWidget::onImportFromSteam() {
  steamCompanion->requestInventory();
}
void PortfolioWidget::onSteamLoginSuccessful() {}
void PortfolioWidget::onSteamLoginFailed(const QString &) {}

void PortfolioWidget::onSteamInventoryFetched(
    const QVector<SteamInventoryItem> &items) {
  if (loadingDialog) {
    loadingDialog->close();
    loadingDialog->deleteLater();
    loadingDialog = nullptr;
  }
  if (items.isEmpty()) {
    QMessageBox::information(this, "Empty Inventory", "No items found.");
    return;
  }
  showImportDialog(items);
}

void PortfolioWidget::showImportDialog(
    const QVector<SteamInventoryItem> &items) {
  QDialog dialog(this);
  dialog.setWindowTitle("Import Steam Inventory");
  dialog.setMinimumSize(700, 500);

  QVBoxLayout *layout = new QVBoxLayout(&dialog);
  layout->addWidget(new QLabel(
      QString("Found %1 items. Select items to import:").arg(items.size())));

  QHBoxLayout *selectButtons = new QHBoxLayout();
  QPushButton *selectAll = new QPushButton("Select All");
  QPushButton *selectNone = new QPushButton("Select None");
  selectButtons->addWidget(selectAll);
  selectButtons->addWidget(selectNone);
  selectButtons->addStretch();
  layout->addLayout(selectButtons);

  QTableWidget *table = new QTableWidget(&dialog);
  table->setColumnCount(5);
  table->setHorizontalHeaderLabels({"", "Name", "Type", "Rarity", "Tradable"});
  table->setRowCount(items.size());
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

  for (int i = 0; i < items.size(); ++i) {
    const SteamInventoryItem &item = items[i];
    QTableWidgetItem *check = new QTableWidgetItem();
    check->setCheckState(Qt::Checked);
    table->setItem(i, 0, check);
    table->setItem(i, 1, new QTableWidgetItem(item.marketHashName));
    table->setItem(i, 2, new QTableWidgetItem(item.type));
    table->setItem(i, 3, new QTableWidgetItem(item.rarity));
    table->setItem(i, 4, new QTableWidgetItem(item.tradable ? "Yes" : "No"));
  }
  table->setColumnWidth(0, 30);
  layout->addWidget(table);

  QCheckBox *fetchPrices = new QCheckBox("Fetch current prices after import");
  fetchPrices->setChecked(api->isValid());
  layout->addWidget(fetchPrices);

  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout->addWidget(&buttons);

  connect(selectAll, &QPushButton::clicked, [table]() {
    for (int i = 0; i < table->rowCount(); ++i)
      table->item(i, 0)->setCheckState(Qt::Checked);
  });
  connect(selectNone, &QPushButton::clicked, [table]() {
    for (int i = 0; i < table->rowCount(); ++i)
      table->item(i, 0)->setCheckState(Qt::Unchecked);
  });
  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted)
    return;

  QVector<PortfolioItem> importItems;
  for (int i = 0; i < items.size(); ++i) {
    if (table->item(i, 0)->checkState() != Qt::Checked)
      continue;
    const SteamInventoryItem &si = items[i];

    PortfolioItem pi;
    pi.skinName = si.marketHashName;
    if (!si.exterior.isEmpty()) {
      pi.condition = si.exterior;
    } else {
      QRegularExpression rx(R"(\(([^)]+)\)$)");
      QRegularExpressionMatch m = rx.match(si.marketHashName);
      pi.condition = m.hasMatch() ? m.captured(1) : "Unknown";
    }
    pi.floatValue = 0.0;
    pi.quantity = 1;
    pi.buyPrice = 0;
    pi.currentPrice = 0;
    pi.purchaseDate = QDate::currentDate().toString("yyyy-MM-dd");
    pi.iconUrl = si.iconUrl;
    pi.rarity = si.rarity;
    importItems.append(pi);
  }

  portfolioManager->importFromSteamInventory(currentPortfolioId, importItems);
  loadPortfolioItems();
  emit portfolioUpdated();

  if (fetchPrices->isChecked() && api->arePricesLoaded()) {
    Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);
    int startIndex = portfolio.items.size() - importItems.size();
    int updated = 0;

    for (int i = 0; i < importItems.size(); ++i) {
      PortfolioItem item = portfolio.items[startIndex + i];
      double price = api->fetchPrice(item.skinName);
      if (price > 0.0) {
        item.currentPrice = price;
        portfolioManager->updateItem(currentPortfolioId, startIndex + i, item);
        ++updated;
      }
    }
    loadPortfolioItems();
    QMessageBox::information(
        this, "Import Complete",
        QString("Imported %1 items, priced %2 instantly from cache.")
            .arg(importItems.size())
            .arg(updated));
  } else {
    QMessageBox::information(
        this, "Import Complete",
        QString("Imported %1 items.").arg(importItems.size()));
  }
}

// ---------------------------------------------------------------------------
// GC / Steam companion slots
// ---------------------------------------------------------------------------

void PortfolioWidget::onGCLoginClicked() {
  if (gcStatusLabel)
    gcStatusLabel->setText("Fetching inventory...");
  steamCompanion->requestInventory();
}
void PortfolioWidget::onGCImportClicked() {
  if (gcStatusLabel)
    gcStatusLabel->setText("Fetching inventory...");
  steamCompanion->requestInventory();
}

void PortfolioWidget::onGCStorageUnitClicked() {
  int idx = gcStorageUnitCombo->currentIndex();
  if (idx < 0 || idx >= gcContainers.size())
    return;
  QString casketId = gcContainers[idx].id;
  gcStatusLabel->setText(
      QString("Fetching storage unit: %1...").arg(gcContainers[idx].name));
  steamCompanion->requestStorageUnit(casketId);
}

void PortfolioWidget::onGCInventoryReceived(
    const QList<GCItem> &items, const QList<GCContainer> &containers) {
  if (!containers.isEmpty()) {
    gcContainers = containers;
    gcStorageUnitCombo->clear();
    for (const GCContainer &c : containers)
      gcStorageUnitCombo->addItem(c.name, c.id);
    gcStorageUnitCombo->setEnabled(true);
    gcStorageUnitButton->setEnabled(true);
  }

  if (items.isEmpty())
    return;

  lastFetchedItems = items;

  // Build assetid → market_hash_name for float matching
  m_assetidToMarketName.clear();
  for (const GCItem &item : items) {
    if (!item.id.isEmpty() && !item.marketHashName.isEmpty())
      m_assetidToMarketName[item.id] = item.marketHashName;
  }

  // Kick off float fetch for backpack items that have an inspect link
  QList<GCItem> floatCandidates;
  for (const GCItem &item : items) {
    if (!item.inspectLink.isEmpty() && item.casketId.isEmpty())
      floatCandidates.append(item);
  }
  if (!floatCandidates.isEmpty())
    steamCompanion->requestFloats(floatCandidates);

  gcStatusLabel->setText(
      QString("Found %1 items. Click 'Select Items to Import' when ready.")
          .arg(items.size()));
  gcShowImportDialogButton->setEnabled(true);
}

void PortfolioWidget::onGCStorageUnitReceived(const QString & /*casketId*/,
                                              const QList<GCItem> &items) {
  if (gcStatusLabel)
    gcStatusLabel->setText(
        QString("Storage unit loaded: %1 items.").arg(items.size()));

  QVector<SteamInventoryItem> steamItems;
  for (const GCItem &gcItem : items) {
    SteamInventoryItem si;
    si.marketHashName =
        gcItem.marketHashName.isEmpty() ? gcItem.name : gcItem.marketHashName;
    si.exterior = gcItem.exterior;
    si.iconUrl = gcItem.iconUrl;
    si.tradable = true;
    si.type = QString::number(gcItem.defIndex);
    si.rarity = QString::number(gcItem.rarity);
    steamItems.append(si);
  }
  // Storage unit items are not directly imported into portfolio here;
  // the Sto
  // rageUnitWidget handles moves. We just surface them if needed.
}

void PortfolioWidget::onFloatsReceived(const QMap<QString, GCItem> &updates) {
  if (currentPortfolioId.isEmpty())
    return;

  bool changed = false;
  for (auto it = updates.constBegin(); it != updates.constEnd(); ++it) {
    const GCItem &gcItem = it.value();
    if (gcItem.paintWear <= 0.0)
      continue;

    QString marketHashName = m_assetidToMarketName.value(it.key());
    if (marketHashName.isEmpty())
      continue;

    Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);
    for (int i = 0; i < portfolio.items.size(); ++i) {
      PortfolioItem pi = portfolio.items[i];
      if (pi.skinName == marketHashName && pi.floatValue <= 0.0) {
        pi.floatValue = gcItem.paintWear;
        portfolioManager->updateItem(currentPortfolioId, i, pi);
        changed = true;
        break; // only update first match per assetid
      }
    }
  }

  if (changed)
    populateTable();
}

QString PortfolioWidget::marketName(const PortfolioItem &item) const {
  if (item.skinName.contains("(" + item.condition + ")"))
    return item.skinName;
  return item.skinName + " (" + item.condition + ")";
}
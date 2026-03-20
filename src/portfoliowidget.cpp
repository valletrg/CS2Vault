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
#include <QThread>
#include <QVBoxLayout>

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
                                 QWidget *parent)
    : QWidget(parent), api(api), steamApi(steamApi),
      portfolioManager(portfolioManager), autoSaveTimer(new QTimer(this)),
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

  // Chart toggle
  toggleChartButton =
      new QPushButton(QString::fromUtf8("\xe2\x96\xb6  Show Chart"), this);
  toggleChartButton->setCheckable(true);
  toggleChartButton->setChecked(false);
  toggleChartButton->setFixedHeight(26);
  toggleChartButton->setStyleSheet(
      "QPushButton { text-align: left; padding-left: 8px; background: "
      "#1e2130;"
      "              border: 1px solid #373a48; border-radius: 5px; color: "
      "#bbb; font-size: 11px; }"
      "QPushButton:checked { background: #252840; color: #e0e0e0; }"
      "QPushButton:hover   { background: #252840; }");
  mainLayout->addWidget(toggleChartButton);

  // Chart container (collapsed by default)
  chartContainer = new QWidget(this);
  chartContainer->hide();
  QVBoxLayout *chartContainerLayout = new QVBoxLayout(chartContainer);
  chartContainerLayout->setContentsMargins(0, 0, 0, 0);

  portfolioChart = new QChart();
  portfolioChart->setTitle("Portfolio Value Over Time");
  portfolioChart->setTitleFont(QFont("Segoe UI", 11, QFont::Bold));
  portfolioChart->setTitleBrush(QBrush(QColor(220, 220, 220)));
  portfolioChart->setAnimationOptions(QChart::SeriesAnimations);
  portfolioChart->setAnimationDuration(600);
  portfolioChart->legend()->setAlignment(Qt::AlignBottom);
  portfolioChart->legend()->setFont(QFont("Segoe UI", 9));
  portfolioChart->legend()->setLabelColor(QColor(200, 200, 200));
  portfolioChart->setBackgroundBrush(QBrush(QColor(28, 30, 38)));
  portfolioChart->setBackgroundPen(QPen(QColor(55, 58, 72), 1));
  portfolioChart->setPlotAreaBackgroundBrush(QBrush(QColor(22, 24, 30)));
  portfolioChart->setPlotAreaBackgroundVisible(true);

  valueSeries = new QLineSeries();
  valueSeries->setName("Total Value");
  QPen valuePen(QColor(56, 200, 120));
  valuePen.setWidth(2);
  valueSeries->setPen(valuePen);

  costSeries = new QLineSeries();
  costSeries->setName("Total Cost (Buy-In)");
  QPen costPen(QColor(90, 155, 230));
  costPen.setWidth(2);
  costSeries->setPen(costPen);

  portfolioChart->addSeries(valueSeries);
  portfolioChart->addSeries(costSeries);

  axisX = new QDateTimeAxis();
  axisX->setTickCount(6);
  axisX->setFormat("MMM d");
  axisX->setTitleText("Date");
  axisX->setTitleFont(QFont("Segoe UI", 9));
  axisX->setTitleBrush(QBrush(QColor(160, 160, 170)));
  axisX->setLabelsFont(QFont("Segoe UI", 8));
  axisX->setLabelsColor(QColor(160, 160, 170));
  axisX->setLinePen(QPen(QColor(60, 63, 80)));
  axisX->setGridLineColor(QColor(45, 48, 62));
  axisX->setShadesVisible(false);
  portfolioChart->addAxis(axisX, Qt::AlignBottom);
  valueSeries->attachAxis(axisX);
  costSeries->attachAxis(axisX);

  axisY = new QValueAxis();
  axisY->setLabelFormat("$%.0f");
  axisY->setTitleText("Value (USD)");
  axisY->setTitleFont(QFont("Segoe UI", 9));
  axisY->setTitleBrush(QBrush(QColor(160, 160, 170)));
  axisY->setLabelsFont(QFont("Segoe UI", 8));
  axisY->setLabelsColor(QColor(160, 160, 170));
  axisY->setLinePen(QPen(QColor(60, 63, 80)));
  axisY->setGridLineColor(QColor(45, 48, 62));
  axisY->setMinorGridLineColor(QColor(35, 38, 50));
  axisY->setMinorGridLineVisible(true);
  axisY->setMinorTickCount(3);
  axisY->setShadesVisible(false);
  portfolioChart->addAxis(axisY, Qt::AlignLeft);
  valueSeries->attachAxis(axisY);
  costSeries->attachAxis(axisY);

  chartView = new QChartView(portfolioChart);
  chartView->setRenderHint(QPainter::Antialiasing);
  chartView->setMinimumHeight(300);
  chartView->setStyleSheet("background: transparent;");
  chartContainerLayout->addWidget(chartView);
  mainLayout->addWidget(chartContainer);

  connect(toggleChartButton, &QPushButton::toggled, this, [this](bool checked) {
    if (!chartContainer)
      return;
    if (checked) {
      chartContainer->show();
      toggleChartButton->setText(QString::fromUtf8("\xe2\x96\xbc  Hide Chart"));
      updateChart();
    } else {
      chartContainer->hide();
      toggleChartButton->setText(QString::fromUtf8("\xe2\x96\xb6  Show Chart"));
    }
  });

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

  connect(gcLoginButton, &QPushButton::clicked, this,
          &PortfolioWidget::onGCLoginClicked);
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
    QTableWidgetItem *floatItem =
        new QTableWidgetItem(QString::number(item.floatValue, 'f', 6));

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

void PortfolioWidget::updateChart() {
  valueSeries->clear();
  costSeries->clear();

  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);
  if (portfolio.history.isEmpty())
    return;

  double minV = std::numeric_limits<double>::max();
  double maxV = std::numeric_limits<double>::lowest();

  for (const PortfolioHistoryPoint &pt : portfolio.history) {
    valueSeries->append(pt.timestamp, pt.totalValue);
    costSeries->append(pt.timestamp, pt.totalCost);
    minV = std::min(minV, std::min(pt.totalValue, pt.totalCost));
    maxV = std::max(maxV, std::max(pt.totalValue, pt.totalCost));
  }

  if (!portfolio.history.isEmpty()) {
    axisX->setRange(
        QDateTime::fromMSecsSinceEpoch(portfolio.history.first().timestamp),
        QDateTime::fromMSecsSinceEpoch(portfolio.history.last().timestamp));
  }

  double padding = (maxV - minV) * 0.1;
  axisY->setRange(std::max(0.0, minV - padding), maxV + padding);

  QPen vPen(maxV >= 0 ? QColor(56, 200, 120) : QColor(220, 70, 70));
  vPen.setWidth(2);
  valueSeries->setPen(vPen);
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
  portfolioManager->recordHistoryPoint(currentPortfolioId);
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
    portfolioManager->recordHistoryPoint(currentPortfolioId);
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

  form.addRow("Weapon:", &weaponCombo);
  form.addRow("Skin Name:", &skinEdit);
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

  auto reply =
      QMessageBox::question(this, "Confirm Remove", "Remove this item?",
                            QMessageBox::Yes | QMessageBox::No);
  if (reply == QMessageBox::Yes) {
    portfolioManager->removeItem(currentPortfolioId, dataIndex);
    loadPortfolioItems();
    emit portfolioUpdated();
  }
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

QString PortfolioWidget::marketName(const PortfolioItem &item) const {
  if (item.skinName.contains("(" + item.condition + ")"))
    return item.skinName;
  return item.skinName + " (" + item.condition + ")";
}
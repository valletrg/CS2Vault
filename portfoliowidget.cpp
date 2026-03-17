#include "portfoliowidget.h"
#include "portfoliomanager.h"
#include "priceempireapi.h"
#include "steamapi.h"

#include "qrlogindialog.h"
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

class NumericTableWidgetItem : public QTableWidgetItem {
public:
  using QTableWidgetItem::QTableWidgetItem;
  bool operator<(const QTableWidgetItem &other) const override {
    QVariant myData = data(Qt::UserRole);
    QVariant otherData = other.data(Qt::UserRole);
    if (myData.isValid() && otherData.isValid()) {
      return myData.toDouble() < otherData.toDouble();
    }
    return QTableWidgetItem::operator<(other);
  }
};

PortfolioWidget::PortfolioWidget(PriceEmpireAPI *api, SteamAPI *steamApi,
                                 PortfolioManager *portfolioManager,
                                 QWidget *parent)
    : QWidget(parent), api(api), steamApi(steamApi),
      portfolioManager(portfolioManager), autoSaveTimer(new QTimer(this)),
      loadingDialog(nullptr) {
  setupUI();

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
            steamStatusLabel->setText("Error: " + error);
            steamStatusLabel->setStyleSheet("color: #DC4646;");
          });

  connect(portfolioManager, &PortfolioManager::portfolioChanged, this,
          [this](const QString &id) {
            if (id == currentPortfolioId) {
              loadPortfolioItems();
            }
          });

  refreshPortfolioList();

  connect(autoSaveTimer, &QTimer::timeout, portfolioManager,
          &PortfolioManager::saveToFile);
  autoSaveTimer->start(30000);
  priceCheckTimer = new QTimer(this);
  priceCheckTimer->setInterval(2000); // one request per 2 seconds
  connect(priceCheckTimer, &QTimer::timeout, this,
          &PortfolioWidget::onPriceCheckTick);
}

PortfolioWidget::~PortfolioWidget() {
  if (loadingDialog) {
    loadingDialog->close();
    loadingDialog->deleteLater();
  }
}

void PortfolioWidget::setupUI() {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);

  QHBoxLayout *portfolioSelectLayout = new QHBoxLayout();

  QLabel *portfolioLabel = new QLabel("Portfolio:", this);
  portfolioComboBox = new QComboBox(this);
  portfolioComboBox->setMinimumWidth(200);

  createPortfolioButton = new QPushButton("New", this);
  deletePortfolioButton = new QPushButton("Delete", this);
  renamePortfolioButton = new QPushButton("Rename", this);

  portfolioSelectLayout->addWidget(portfolioLabel);
  portfolioSelectLayout->addWidget(portfolioComboBox, 1);
  portfolioSelectLayout->addWidget(createPortfolioButton);
  portfolioSelectLayout->addWidget(renamePortfolioButton);
  portfolioSelectLayout->addWidget(deletePortfolioButton);

  mainLayout->addLayout(portfolioSelectLayout);

  QSplitter *splitter = new QSplitter(Qt::Vertical, this);

  QWidget *topWidget = new QWidget(splitter);
  QVBoxLayout *topLayout = new QVBoxLayout(topWidget);
  topLayout->setContentsMargins(0, 0, 0, 0);

  portfolioTable = new QTableWidget(topWidget);
  portfolioTable->setColumnCount(8);
  portfolioTable->setHorizontalHeaderLabels({"Skin Name", "Condition", "Float",
                                             "Qty", "Buy Price", "Current",
                                             "Profit/Loss", "ROI %"});
  portfolioTable->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Stretch);
  portfolioTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  portfolioTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  portfolioTable->setSortingEnabled(true);

  topLayout->addWidget(portfolioTable);

  QHBoxLayout *buttonLayout = new QHBoxLayout();

  addButton = new QPushButton("Add Item", topWidget);
  removeButton = new QPushButton("Remove", topWidget);
  editButton = new QPushButton("Edit", topWidget);
  exportButton = new QPushButton("Export CSV", topWidget);
  importButton = new QPushButton("Import CSV", topWidget);
  refreshButton = new QPushButton("Refresh Prices", topWidget);

  removeButton->setEnabled(false);
  editButton->setEnabled(false);

  buttonLayout->addWidget(addButton);
  buttonLayout->addWidget(removeButton);
  buttonLayout->addWidget(editButton);
  buttonLayout->addStretch();
  buttonLayout->addWidget(importButton);
  buttonLayout->addWidget(exportButton);
  buttonLayout->addWidget(refreshButton);

  topLayout->addLayout(buttonLayout);

  QWidget *bottomWidget = new QWidget(splitter);
  QVBoxLayout *bottomLayout = new QVBoxLayout(bottomWidget);
  bottomLayout->setContentsMargins(0, 0, 0, 0);

  // Stats row: stat labels on the left, Steam import button on the right
  QHBoxLayout *statsAndSteam = new QHBoxLayout();

  // Steam GC Import panel
  QGroupBox *steamGroup = new QGroupBox("Steam Import", bottomWidget);
  QVBoxLayout *steamLayout = new QVBoxLayout(steamGroup);

  gcStatusLabel = new QLabel("Not connected", steamGroup);
  steamStatusLabel = gcStatusLabel; // alias so old slots don't crash
  gcStatusLabel->setWordWrap(true);
  steamLayout->addWidget(gcStatusLabel);

  gcLoginButton = new QPushButton("Sign in with Steam QR", steamGroup);
  gcLoginButton->setFixedHeight(32);
  steamLayout->addWidget(gcLoginButton);

  gcImportButton = new QPushButton("Import Inventory", steamGroup);
  gcImportButton->setEnabled(false);
  gcImportButton->setFixedHeight(32);
  steamLayout->addWidget(gcImportButton);

  QHBoxLayout *storageLayout = new QHBoxLayout();
  gcStorageUnitCombo = new QComboBox(steamGroup);
  gcStorageUnitCombo->setEnabled(false);
  gcStorageUnitCombo->setPlaceholderText("No storage units");
  gcStorageUnitButton = new QPushButton("Import Unit", steamGroup);
  gcStorageUnitButton->setEnabled(false);
  storageLayout->addWidget(gcStorageUnitCombo, 1);
  storageLayout->addWidget(gcStorageUnitButton);
  steamLayout->addLayout(storageLayout);

  statsAndSteam->addWidget(steamGroup);

  bottomLayout->addLayout(statsAndSteam);

  // Wire up the Steam import button to open a compact dialog
  steamCompanion = new SteamCompanion(this);

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
            gcStatusLabel->setText(QString("Logged in (%1)").arg(steamId));
            gcStatusLabel->setStyleSheet("color: green;");
          });

  connect(steamCompanion, &SteamCompanion::gcReady, this, [this]() {
    gcImportButton->setEnabled(true);
    gcStatusLabel->setText("Connected to CS2 GC — ready to import.");
  });

  connect(steamCompanion, &SteamCompanion::statusMessage, this,
          [this](const QString &msg) { gcStatusLabel->setText(msg); });

  connect(steamCompanion, &SteamCompanion::errorOccurred, this,
          [this](const QString &err) {
            gcStatusLabel->setText("Error: " + err);
            gcStatusLabel->setStyleSheet("color: red;");
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

  // -- Chart setup --
  portfolioChart = new QChart();
  portfolioChart->setTitle("Portfolio Value Over Time");
  portfolioChart->setTitleFont(QFont("Segoe UI", 11, QFont::Bold));
  portfolioChart->setTitleBrush(QBrush(QColor(220, 220, 220)));
  portfolioChart->setAnimationOptions(QChart::SeriesAnimations);
  portfolioChart->setAnimationDuration(600);
  portfolioChart->legend()->setAlignment(Qt::AlignBottom);
  portfolioChart->legend()->setFont(QFont("Segoe UI", 9));
  portfolioChart->legend()->setLabelColor(QColor(200, 200, 200));

  // Dark chart background
  portfolioChart->setBackgroundBrush(QBrush(QColor(28, 30, 38)));
  portfolioChart->setBackgroundPen(QPen(QColor(55, 58, 72), 1));
  portfolioChart->setPlotAreaBackgroundBrush(QBrush(QColor(22, 24, 30)));
  portfolioChart->setPlotAreaBackgroundVisible(true);

  // Value series (green gradient area)
  valueSeries = new QLineSeries();
  valueSeries->setName("Total Value");
  QPen valuePen(QColor(56, 200, 120));
  valuePen.setWidth(2);
  valueSeries->setPen(valuePen);

  // Cost series (blue gradient area)
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
  chartView->setMinimumHeight(350);
  chartView->setStyleSheet("background: transparent;");

  bottomLayout->addWidget(chartView, 1);

  splitter->addWidget(topWidget);
  splitter->addWidget(bottomWidget);
  splitter->setStretchFactor(0, 2);
  splitter->setStretchFactor(1, 3); // give more height to bottom (chart) pane

  mainLayout->addWidget(splitter);

  // Price check queue panel -- hidden by default, shown when a job is active
  queueGroup = new QWidget(this);
  QHBoxLayout *queueLayout = new QHBoxLayout(queueGroup);
  queueLayout->setContentsMargins(4, 2, 4, 2);

  queueStatusLabel = new QLabel("No price checks pending.", queueGroup);
  queuePauseButton = new QPushButton("Pause", queueGroup);
  queuePauseButton->setEnabled(false);

  queueLayout->addWidget(queueStatusLabel, 1);
  queueLayout->addWidget(queuePauseButton);

  queueGroup->hide(); // only visible while a price check batch is in flight
  mainLayout->addWidget(queueGroup);

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

  // steamLoginButton and importSteamButton are handled via the Steam Import
  // dialog now; no direct connects needed here for them.

  connect(portfolioTable, &QTableWidget::itemSelectionChanged, this, [this]() {
    bool hasSelection = portfolioTable->currentRow() >= 0;
    removeButton->setEnabled(hasSelection);
    editButton->setEnabled(hasSelection);
  });
}

void PortfolioWidget::refreshPortfolioList() {
  portfolioComboBox->clear();

  QVector<Portfolio> portfolios = portfolioManager->getAllPortfolios();
  for (const Portfolio &portfolio : portfolios) {
    portfolioComboBox->addItem(portfolio.name, portfolio.id);
  }

  if (!portfolios.isEmpty()) {
    currentPortfolioId = portfolios.first().id;
    loadPortfolioItems();
    // Seed the first history dot if the portfolio already has items but no
    // history yet
    Portfolio p = portfolioManager->getPortfolio(currentPortfolioId);
    if (!p.items.isEmpty() && p.history.isEmpty()) {
      portfolioManager->recordHistoryPoint(currentPortfolioId);
    }
  }
}

void PortfolioWidget::loadPortfolioItems() {
  populateTable();
  calculateStatistics();
}

void PortfolioWidget::populateTable() {
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
    QTableWidgetItem *conditionItem = new QTableWidgetItem(item.condition);
    QTableWidgetItem *floatItem =
        new QTableWidgetItem(QString::number(item.floatValue, 'f', 6));

    NumericTableWidgetItem *qtyItem =
        new NumericTableWidgetItem(QString::number(item.quantity));
    qtyItem->setData(Qt::UserRole, item.quantity);

    NumericTableWidgetItem *buyItem = new NumericTableWidgetItem(
        QString("$%1").arg(item.buyPrice, 0, 'f', 2));
    buyItem->setData(Qt::UserRole, item.buyPrice);

    NumericTableWidgetItem *currentItem = new NumericTableWidgetItem(
        QString("$%1").arg(item.currentPrice, 0, 'f', 2));
    currentItem->setData(Qt::UserRole, item.currentPrice);

    NumericTableWidgetItem *profitItem =
        new NumericTableWidgetItem(QString("$%1").arg(profit, 0, 'f', 2));
    profitItem->setData(Qt::UserRole, profit);
    profitItem->setForeground(profit >= 0 ? QColor(0, 150, 0)
                                          : QColor(200, 0, 0));

    NumericTableWidgetItem *roiItem =
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
  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);

  double totalInvestment = 0.0;
  double currentValue = 0.0;

  for (const PortfolioItem &item : portfolio.items) {
    totalInvestment += item.buyPrice * item.quantity;
    currentValue += item.currentPrice * item.quantity;
  }

  double profit = currentValue - totalInvestment;
  double roi = totalInvestment > 0 ? (profit / totalInvestment) * 100.0 : 0.0;

  totalInvestmentLabel->setText(QString("$%1").arg(totalInvestment, 0, 'f', 2));
  currentValueLabel->setText(QString("$%1").arg(currentValue, 0, 'f', 2));

  totalProfitLabel->setText(QString("$%1").arg(profit, 0, 'f', 2));
  totalProfitLabel->setStyleSheet(profit >= 0 ? "color: green;"
                                              : "color: red;");

  roiLabel->setText(QString("%1%").arg(roi, 0, 'f', 2));
  roiLabel->setStyleSheet(roi >= 0 ? "color: green;" : "color: red;");

  updateChart();
}

void PortfolioWidget::updateChart() {
  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);

  valueSeries->clear();
  costSeries->clear();

  if (portfolio.history.isEmpty() && portfolio.items.isEmpty()) {
    return;
  }

  double minVal = 1e12;
  double maxVal = 0;
  qint64 minTime = 253402300799000LL;
  qint64 maxTime = 0;

  for (const PortfolioHistoryPoint &pt : portfolio.history) {
    valueSeries->append(pt.timestamp * 1000, pt.totalValue);
    costSeries->append(pt.timestamp * 1000, pt.totalCost);

    if (pt.totalValue > maxVal)
      maxVal = pt.totalValue;
    if (pt.totalCost > maxVal)
      maxVal = pt.totalCost;
    if (pt.totalValue < minVal)
      minVal = pt.totalValue;
    if (pt.totalCost < minVal)
      minVal = pt.totalCost;
    if (pt.timestamp * 1000 > maxTime)
      maxTime = pt.timestamp * 1000;
    if (pt.timestamp * 1000 < minTime)
      minTime = pt.timestamp * 1000;
  }

  // Always plot the current live state as the rightmost point
  double currentCost = 0;
  double currentValue = 0;
  for (const PortfolioItem &item : portfolio.items) {
    currentCost += item.buyPrice * item.quantity;
    currentValue += item.currentPrice * item.quantity;
  }

  qint64 nowMs = QDateTime::currentSecsSinceEpoch() * 1000;
  valueSeries->append(nowMs, currentValue);
  costSeries->append(nowMs, currentCost);

  if (currentValue > maxVal)
    maxVal = currentValue;
  if (currentCost > maxVal)
    maxVal = currentCost;
  if (currentValue < minVal)
    minVal = currentValue;
  if (currentCost < minVal)
    minVal = currentCost;
  if (nowMs > maxTime)
    maxTime = nowMs;
  if (nowMs < minTime)
    minTime = nowMs;

  if (minVal == 1e12)
    minVal = 0;

  double padding = (maxVal - minVal) * 0.12;
  if (padding < 5.0)
    padding = 5.0;

  axisY->setRange(qMax(0.0, minVal - padding), maxVal + padding);
  axisY->applyNiceNumbers();

  QDateTime minDt = QDateTime::fromMSecsSinceEpoch(minTime);
  QDateTime maxDt = QDateTime::fromMSecsSinceEpoch(maxTime);

  if (minTime == maxTime) {
    // Only one point — show a ±1-day window so the dot is centered
    minDt = minDt.addDays(-1);
    maxDt = maxDt.addDays(1);
    axisX->setFormat("MMM d");
  } else {
    qint64 span = maxTime - minTime;
    qint64 pad = qMax(span / 20, (qint64)3600000); // at least 1h
    minDt = QDateTime::fromMSecsSinceEpoch(minTime - pad);
    maxDt = QDateTime::fromMSecsSinceEpoch(maxTime + pad);

    // Adaptive date format
    qint64 spanDays = span / 86400000;
    if (spanDays <= 2)
      axisX->setFormat("MMM d hh:mm");
    else if (spanDays <= 90)
      axisX->setFormat("MMM d");
    else
      axisX->setFormat("MMM yyyy");
  }

  axisX->setRange(minDt, maxDt);

  // Re-colour value line green or red based on whether we're in profit
  bool inProfit = (currentValue >= currentCost);
  QPen vPen(inProfit ? QColor(56, 200, 120) : QColor(220, 70, 70));
  vPen.setWidth(2);
  valueSeries->setPen(vPen);
}

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

    int index = portfolioComboBox->findData(id);
    if (index >= 0) {
      portfolioComboBox->setCurrentIndex(index);
    }
  }
}

void PortfolioWidget::onDeletePortfolio() {
  if (currentPortfolioId.isEmpty())
    return;

  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);

  QMessageBox::StandardButton reply = QMessageBox::question(
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

void PortfolioWidget::onAddItem() {
  QDialog dialog(this);
  dialog.setWindowTitle("Add Item");
  dialog.setMinimumWidth(450);
  QFormLayout form(&dialog);

  // Weapon type dropdown
  QComboBox weaponCombo;
  QStringList weapons = {
      // Rifles
      "AK-47", "M4A4", "M4A1-S", "AWP", "FAMAS", "Galil AR", "AUG", "SG 553",
      "SSG 08", "G3SG1", "SCAR-20",
      // Pistols
      "Desert Eagle", "USP-S", "Glock-18", "P2000", "P250", "Five-SeveN",
      "CZ75-Auto", "Tec-9", "Dual Berettas", "R8 Revolver",
      // SMGs
      "MP9", "MAC-10", "PP-Bizon", "MP7", "UMP-45", "P90", "MP5-SD",
      // Heavy
      "Nova", "XM1014", "Sawed-Off", "MAG-7", "M249", "Negev",
      // Knives
      "★ Karambit", "★ M9 Bayonet", "★ Bayonet", "★ Butterfly Knife",
      "★ Falchion Knife", "★ Shadow Daggers", "★ Gut Knife", "★ Flip Knife",
      "★ Navaja Knife", "★ Stiletto Knife", "★ Talon Knife", "★ Ursus Knife",
      "★ Huntsman Knife", "★ Bowie Knife", "★ Skeleton Knife", "★ Kukri Knife",
      // Gloves
      "★ Sport Gloves", "★ Specialist Gloves", "★ Moto Gloves",
      "★ Bloodhound Gloves", "★ Hand Wraps", "★ Hydra Gloves",
      "★ Driver Gloves", "★ Broken Fang Gloves",
      // Other
      "Zeus x27", "MP5-SD"};
  weaponCombo.addItems(weapons);

  // Skin name - just the name part e.g. "Redline"
  QLineEdit skinNameEdit;
  skinNameEdit.setPlaceholderText("e.g. Redline, Asiimov, Fade...");

  // Preview label showing assembled market name
  QLabel previewLabel;
  previewLabel.setStyleSheet("color: gray; font-style: italic;");
  previewLabel.setText("AK-47 | Redline");

  QComboBox conditionCombo;
  conditionCombo.addItems({"Factory New", "Minimal Wear", "Field-Tested",
                           "Well-Worn", "Battle-Scarred"});

  QDoubleSpinBox floatSpinBox;
  floatSpinBox.setRange(0.0, 1.0);
  floatSpinBox.setDecimals(6);
  floatSpinBox.setValue(0.25);

  QSpinBox quantitySpinBox;
  quantitySpinBox.setRange(1, 100);

  QDoubleSpinBox buyPriceSpinBox;
  buyPriceSpinBox.setRange(0.0, 100000.0);
  buyPriceSpinBox.setPrefix("$ ");
  buyPriceSpinBox.setDecimals(2);

  QLineEdit notesEdit;

  // Lambda to rebuild the preview whenever weapon or skin name changes
  auto updatePreview = [&]() {
    QString weapon = weaponCombo.currentText();
    QString skin = skinNameEdit.text().trimmed();

    QString preview;
    if (skin.isEmpty()) {
      preview = weapon;
    } else if (weapon.startsWith("★")) {
      // Knives/gloves: "★ Karambit | Fade"
      preview = weapon + " | " + skin;
    } else {
      preview = weapon + " | " + skin;
    }
    previewLabel.setText("Market name: " + preview);
  };

  connect(&weaponCombo, &QComboBox::currentTextChanged,
          [&](const QString &) { updatePreview(); });
  connect(&skinNameEdit, &QLineEdit::textChanged,
          [&](const QString &) { updatePreview(); });

  form.addRow("Weapon:", &weaponCombo);
  form.addRow("Skin Name:", &skinNameEdit);
  form.addRow("", &previewLabel);
  form.addRow("Condition:", &conditionCombo);
  form.addRow("Float:", &floatSpinBox);
  form.addRow("Quantity:", &quantitySpinBox);
  form.addRow("Buy Price:", &buyPriceSpinBox);
  form.addRow("Notes:", &notesEdit);

  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  form.addRow(&buttons);

  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() == QDialog::Accepted && !skinNameEdit.text().isEmpty()) {
    QString weapon = weaponCombo.currentText();
    QString skin = skinNameEdit.text().trimmed();
    QString condition = conditionCombo.currentText();

    // Assemble the market_hash_name
    QString marketName = weapon + " | " + skin + " (" + condition + ")";

    PortfolioItem item;
    item.skinName = weapon + " | " + skin; // stored without condition
    item.condition = condition;
    item.floatValue = floatSpinBox.value();
    item.quantity = quantitySpinBox.value();
    item.buyPrice = buyPriceSpinBox.value();
    item.currentPrice = 0;
    item.purchaseDate = QDate::currentDate().toString("yyyy-MM-dd");
    item.notes = notesEdit.text();

    if (api->isValid()) {
      double price = api->fetchPrice(marketName);
      if (price > 0) {
        item.currentPrice = price;
      }
    }

    if (item.currentPrice <= 0) {
      item.currentPrice = item.buyPrice;
    }

    portfolioManager->addItem(currentPortfolioId, item);
    loadPortfolioItems();
    emit portfolioUpdated();
  }
}

void PortfolioWidget::onRemoveItem() {
  int row = portfolioTable->currentRow();
  if (row >= 0) {
    QMessageBox::StandardButton reply =
        QMessageBox::question(this, "Confirm Remove", "Remove this item?",
                              QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
      portfolioManager->removeItem(currentPortfolioId, row);
      loadPortfolioItems();
      emit portfolioUpdated();
    }
  }
}

void PortfolioWidget::onEditItem() {
  int row = portfolioTable->currentRow();
  if (row < 0)
    return;

  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);
  if (row >= portfolio.items.size())
    return;

  const PortfolioItem &currentItem = portfolio.items[row];

  QDialog dialog(this);
  dialog.setWindowTitle("Edit Item");
  dialog.setMinimumWidth(400);
  QFormLayout form(&dialog);

  QLineEdit skinNameEdit(currentItem.skinName);
  QComboBox conditionCombo;
  QDoubleSpinBox floatSpinBox;
  QSpinBox quantitySpinBox;
  QDoubleSpinBox buyPriceSpinBox;
  QLineEdit notesEdit(currentItem.notes);

  conditionCombo.addItems({"Factory New", "Minimal Wear", "Field-Tested",
                           "Well-Worn", "Battle-Scarred"});
  conditionCombo.setCurrentText(currentItem.condition);

  floatSpinBox.setRange(0.0, 1.0);
  floatSpinBox.setDecimals(6);
  floatSpinBox.setValue(currentItem.floatValue);

  quantitySpinBox.setRange(1, 100);
  quantitySpinBox.setValue(currentItem.quantity);

  buyPriceSpinBox.setRange(0.0, 100000.0);
  buyPriceSpinBox.setPrefix("$ ");
  buyPriceSpinBox.setDecimals(2);
  buyPriceSpinBox.setValue(currentItem.buyPrice);

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

  if (dialog.exec() == QDialog::Accepted) {
    PortfolioItem updatedItem = currentItem;
    updatedItem.skinName = skinNameEdit.text();
    updatedItem.condition = conditionCombo.currentText();
    updatedItem.floatValue = floatSpinBox.value();
    updatedItem.quantity = quantitySpinBox.value();
    updatedItem.buyPrice = buyPriceSpinBox.value();
    updatedItem.notes = notesEdit.text();

    portfolioManager->updateItem(currentPortfolioId, row, updatedItem);
    loadPortfolioItems();
    emit portfolioUpdated();
  }
}

void PortfolioWidget::onImportFromSteam() {
  QString steamId = steamIdEdit->text().trimmed();

  if (steamId.isEmpty()) {
    QMessageBox::warning(this, "Steam ID Required",
                         "Please enter your Steam ID.\n\n"
                         "Find it at: https://steamid.io/");
    return;
  }

  steamStatusLabel->setText("Connecting...");
  steamStatusLabel->setStyleSheet("color: blue;");
  steamLoginButton->setEnabled(false);

  steamApi->loginWithSteamId(steamId);
}

void PortfolioWidget::onSteamLoginSuccessful() {
  SteamProfile profile = steamApi->getProfile();

  steamStatusLabel->setText(QString("Connected: %1").arg(profile.personaName));
  steamStatusLabel->setStyleSheet("color: green;");
  steamLoginButton->setEnabled(true);
  importSteamButton->setEnabled(true);
}

void PortfolioWidget::onSteamLoginFailed(const QString &error) {
  steamStatusLabel->setText(QString("Failed: %1").arg(error));
  steamStatusLabel->setStyleSheet("color: red;");
  steamLoginButton->setEnabled(true);
  importSteamButton->setEnabled(false);
}

void PortfolioWidget::onSteamInventoryFetched(
    const QVector<SteamInventoryItem> &items) {
  if (loadingDialog) {
    loadingDialog->close();
    loadingDialog->deleteLater();
    loadingDialog = nullptr;
  }

  steamStatusLabel->setText(QString("Found %1 items").arg(items.size()));

  if (items.isEmpty()) {
    QMessageBox::information(this, "Steam Inventory",
                             "No tradable CS2 items found.");
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

  QLabel *infoLabel = new QLabel(
      QString("Found %1 items. Select items to import:").arg(items.size()));
  layout->addWidget(infoLabel);

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

    QTableWidgetItem *checkItem = new QTableWidgetItem();
    checkItem->setCheckState(Qt::Checked);
    table->setItem(i, 0, checkItem);
    table->setItem(i, 1, new QTableWidgetItem(item.marketHashName));
    table->setItem(i, 2, new QTableWidgetItem(item.type));
    table->setItem(i, 3, new QTableWidgetItem(item.rarity));
    table->setItem(i, 4, new QTableWidgetItem(item.tradable ? "Yes" : "No"));
  }

  table->setColumnWidth(0, 30);
  layout->addWidget(table);

  QCheckBox *fetchPrices = new QCheckBox("Fetch current prices from API");
  fetchPrices->setChecked(api->isValid());
  fetchPrices->setEnabled(api->isValid());
  layout->addWidget(fetchPrices);

  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout->addWidget(&buttons);

  connect(selectAll, &QPushButton::clicked, [table]() {
    for (int i = 0; i < table->rowCount(); ++i) {
      table->item(i, 0)->setCheckState(Qt::Checked);
    }
  });

  connect(selectNone, &QPushButton::clicked, [table]() {
    for (int i = 0; i < table->rowCount(); ++i) {
      table->item(i, 0)->setCheckState(Qt::Unchecked);
    }
  });

  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() == QDialog::Accepted) {
    QVector<PortfolioItem> importItems;

    for (int i = 0; i < items.size(); ++i) {
      if (table->item(i, 0)->checkState() == Qt::Checked) {
        const SteamInventoryItem &steamItem = items[i];

        PortfolioItem portfolioItem;
        portfolioItem.skinName = steamItem.marketHashName;
        if (!steamItem.exterior.isEmpty()) {
          portfolioItem.condition = steamItem.exterior;
        } else {
          // Parse condition from the end of the market hash name
          QRegularExpression rx(R"(\(([^)]+)\)$)");
          QRegularExpressionMatch match = rx.match(steamItem.marketHashName);
          if (match.hasMatch()) {
            portfolioItem.condition = match.captured(1);
          } else {
            portfolioItem.condition = "Unknown";
          }
        }
        portfolioItem.floatValue = 0.0;
        portfolioItem.quantity = 1;
        portfolioItem.buyPrice = 0;
        portfolioItem.currentPrice = 0;
        portfolioItem.purchaseDate =
            QDate::currentDate().toString("yyyy-MM-dd");
        portfolioItem.iconUrl = steamItem.iconUrl;
        portfolioItem.rarity = steamItem.rarity;

        importItems.append(portfolioItem);
      }
    }

    portfolioManager->importFromSteamInventory(currentPortfolioId, importItems);
    loadPortfolioItems();
    emit portfolioUpdated();

    // Queue price checks for all imported items if requested
    if (fetchPrices->isChecked() && api->isValid()) {
      Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);
      int startIndex = portfolio.items.size() - importItems.size();

      priceCheckTotal += importItems.size();
      priceCheckDone = 0;

      for (int i = 0; i < importItems.size(); ++i) {
        const PortfolioItem &item = importItems[i];
        // For Steam imports, skinName is already the full market_hash_name
        // e.g. "P250 | Sleet (Field-Tested)" - don't append condition again
        enqueuePriceCheck(currentPortfolioId, startIndex + i, item.skinName);
      }

      QMessageBox::information(
          this, "Import Complete",
          QString(
              "Imported %1 items.\n\n"
              "Prices will be fetched in the background at ~1 per 2 seconds "
              "to respect Steam's rate limit.\n\n"
              "You can watch progress in the Price Check Queue panel, "
              "and pause it at any time.")
              .arg(importItems.size()));
    } else {
      QMessageBox::information(
          this, "Import Complete",
          QString("Imported %1 items.").arg(importItems.size()));
    }
  }
}

void PortfolioWidget::onRefreshPrices() { updateAllPrices(); }

void PortfolioWidget::updateAllPrices() {
  if (!api->isValid()) {
    QMessageBox::warning(this, "API Error", "API not configured.");
    return;
  }

  Portfolio portfolio = portfolioManager->getPortfolio(currentPortfolioId);

  int updated = 0;
  for (int i = 0; i < portfolio.items.size(); ++i) {
    PortfolioItem item = portfolio.items[i];
    QString marketName = item.skinName + " (" + item.condition + ")";
    double price = api->fetchPrice(marketName);

    if (price > 0) {
      item.currentPrice = price;
      portfolioManager->updateItem(currentPortfolioId, i, item);
      updated++;
    }

    QThread::msleep(100);
  }

  loadPortfolioItems();

  QMessageBox::information(this, "Prices Updated",
                           QString("Updated %1 items.").arg(updated));

  emit portfolioUpdated();
}

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
  out << "Skin Name,Condition,Float,Quantity,Buy Price,Current Price,Purchase "
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
  QString header = in.readLine(); // Skip header

  QVector<PortfolioItem> importItems;
  int imported = 0;

  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    if (line.isEmpty())
      continue;

    // Simple CSV parsing
    QStringList parts;
    QString current;
    bool inQuotes = false;

    for (int i = 0; i < line.length(); ++i) {
      QChar c = line[i];
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
      item.quantity = parts[3].toInt();
      if (item.quantity <= 0)
        item.quantity = 1;
      item.buyPrice = parts[4].toDouble();
      item.currentPrice = parts[5].toDouble();
      item.purchaseDate = parts[6].remove('"');
      if (parts.size() >= 8) {
        item.notes = parts[7].remove('"');
      }

      if (!item.skinName.isEmpty()) {
        importItems.append(item);
        imported++;
      }
    }
  }

  file.close();

  if (!importItems.isEmpty()) {
    portfolioManager->importFromSteamInventory(currentPortfolioId, importItems);
    loadPortfolioItems();

    QMessageBox::information(this, "Import Complete",
                             QString("Imported %1 items.").arg(imported));
    emit portfolioUpdated();
  } else {
    QMessageBox::warning(this, "Import Failed", "No valid items found in CSV.");
  }
}

void PortfolioWidget::enqueuePriceCheck(const QString &portfolioId,
                                        int itemIndex,
                                        const QString &marketName) {
  PriceCheckJob job;
  job.portfolioId = portfolioId;
  job.itemIndex = itemIndex;
  job.marketName = marketName;
  priceCheckQueue.enqueue(job);

  queueGroup->show(); // reveal the progress bar
  updateQueueStatusLabel();
  queuePauseButton->setEnabled(true);

  if (!priceCheckTimer->isActive() && !priceCheckPaused) {
    priceCheckTimer->start();
  }
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
    // Record this as a history snapshot since prices just refreshed
    portfolioManager->recordHistoryPoint(currentPortfolioId);
    // Hide the bar after a short delay so the user sees the "Done" message
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

  // Refresh the table every 5 completed items so the user sees progress
  if (priceCheckDone % 5 == 0) {
    loadPortfolioItems();
  }
}

void PortfolioWidget::updateQueueStatusLabel() {
  int remaining = priceCheckQueue.size();
  if (remaining == 0)
    return;

  // Estimate time remaining: 2 seconds per item
  int secondsLeft = remaining * 2;
  QString timeStr;
  if (secondsLeft < 60) {
    timeStr = QString("%1s").arg(secondsLeft);
  } else {
    timeStr = QString("%1m %2s").arg(secondsLeft / 60).arg(secondsLeft % 60);
  }

  queueStatusLabel->setText(
      QString("Checking prices: %1 done, %2 remaining (~%3 left)")
          .arg(priceCheckDone)
          .arg(remaining)
          .arg(timeStr));
}

void PortfolioWidget::onGCLoginClicked() {
  gcLoginButton->setEnabled(false);
  gcStatusLabel->setText("Starting Steam companion...");
  gcStatusLabel->setStyleSheet("color: gray;");
  steamCompanion->start();
}

void PortfolioWidget::onGCImportClicked() {
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
  gcContainers = containers;
  gcStorageUnitCombo->clear();
  for (const GCContainer &c : containers)
    gcStorageUnitCombo->addItem(c.name, c.id);
  gcStorageUnitCombo->setEnabled(!containers.isEmpty());
  gcStorageUnitButton->setEnabled(!containers.isEmpty());

  gcStatusLabel->setText(QString("Found %1 items, %2 storage unit(s).")
                             .arg(items.size())
                             .arg(containers.size()));

  QVector<SteamInventoryItem> steamItems;
  for (const GCItem &gcItem : items) {
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

  if (!steamItems.isEmpty())
    showImportDialog(steamItems);
}

void PortfolioWidget::onGCStorageUnitReceived(const QString &casketId,
                                              const QList<GCItem> &items) {
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

  if (!steamItems.isEmpty())
    showImportDialog(steamItems);
}

#include "settingswidget.h"
#include "portfoliomanager.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

SettingsWidget::SettingsWidget(PriceEmpireAPI *api,
                               PortfolioManager *portfolioManager,
                               QWidget *parent)
    : QWidget(parent), api(api), portfolioManager(portfolioManager) {
  setupUI();
  loadSettings();
  QTimer::singleShot(500, this, &SettingsWidget::onTestAPI);
}

SettingsWidget::~SettingsWidget() { saveSettings(); }

void SettingsWidget::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  // ── Price data group ──────────────────────────────────────────────────────
  auto *priceGroup = new QGroupBox("Price Data", this);
  auto *priceLayout = new QFormLayout(priceGroup);

  apiStatusLabel = new QLabel("Checking...", priceGroup);
  apiStatusLabel->setStyleSheet("color: #aaa;");

  priceLastUpdatedLabel = new QLabel("—", priceGroup);
  priceLastUpdatedLabel->setStyleSheet("color: #aaa; font-size: 11px;");

  testAPIButton = new QPushButton("Test Connection", priceGroup);
  testAPIButton->setFixedHeight(28);

  priceLayout->addRow("Status:", apiStatusLabel);
  priceLayout->addRow("Last updated:", priceLastUpdatedLabel);
  sourceComboBox = new QComboBox(priceGroup);
  sourceComboBox->addItems({"Skinport", "white.market", "market.csgo.com"});
  priceLayout->addRow("Price source:", sourceComboBox);

  connect(
      sourceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
      [this]() {
        static const QMap<QString, QString> urls = {
            {"Skinport", "https://fursense.lol/prices.json"},
            {"white.market", "https://fursense.lol/prices-whitemarket.json"},
            {"market.csgo.com", "https://fursense.lol/prices-marketcsgo.json"}};
        QString url = urls.value(sourceComboBox->currentText());
        api->setSourceUrl(url);
        apiStatusLabel->setText("Switching...");
        apiStatusLabel->setStyleSheet("color: #5b9bd5;");
        priceLastUpdatedLabel->setText("—");

        // When the new prices finish loading, update the status automatically
        connect(
            api, &PriceEmpireAPI::pricesLoaded, this,
            [this]() { updatePriceStatus(); }, Qt::SingleShotConnection);
      });
  priceLayout->addRow("", testAPIButton);

  mainLayout->addWidget(priceGroup);

  // ── Application settings group ────────────────────────────────────────────
  auto *appGroup = new QGroupBox("Application Settings", this);
  auto *appLayout = new QFormLayout(appGroup);

  autoUpdateCheckBox =
      new QCheckBox("Auto-refresh prices on a timer", appGroup);
  autoUpdateCheckBox->setChecked(true);

  updateIntervalSpinBox = new QSpinBox(appGroup);
  updateIntervalSpinBox->setRange(1, 60);
  updateIntervalSpinBox->setValue(5);
  updateIntervalSpinBox->setSuffix(" minutes");

  currencyComboBox = new QComboBox(appGroup);
  currencyComboBox->addItems({"USD", "EUR", "GBP", "CAD", "AUD"});

  appLayout->addRow(autoUpdateCheckBox);
  appLayout->addRow("Update interval:", updateIntervalSpinBox);
  appLayout->addRow("Currency:", currencyComboBox);

  mainLayout->addWidget(appGroup);

  // ── Data group ────────────────────────────────────────────────────────────
  auto *dataGroup = new QGroupBox("Data", this);
  auto *dataLayout = new QVBoxLayout(dataGroup);

  auto *exportButton =
      new QPushButton("Export All Portfolios to CSV", dataGroup);
  exportButton->setFixedHeight(32);

  auto *dataFolderButton = new QPushButton("Open Data Folder", dataGroup);
  dataFolderButton->setFixedHeight(32);

  auto *dataNote =
      new QLabel("The data folder contains your portfolios.json save file. "
                 "Back it up to keep your portfolio history safe.",
                 dataGroup);
  dataNote->setWordWrap(true);
  dataNote->setStyleSheet("color: #666; font-size: 11px;");

  dataLayout->addWidget(exportButton);
  dataLayout->addWidget(dataFolderButton);
  dataLayout->addWidget(dataNote);

  mainLayout->addWidget(dataGroup);

  // ── Save button ───────────────────────────────────────────────────────────
  auto *buttonLayout = new QHBoxLayout();
  saveButton = new QPushButton("Save Settings", this);
  saveButton->setFixedHeight(32);
  buttonLayout->addStretch();
  buttonLayout->addWidget(saveButton);
  mainLayout->addLayout(buttonLayout);
  mainLayout->addStretch();

  connect(testAPIButton, &QPushButton::clicked, this,
          &SettingsWidget::onTestAPI);
  connect(saveButton, &QPushButton::clicked, this,
          &SettingsWidget::onSaveSettings);
  connect(exportButton, &QPushButton::clicked, this,
          &SettingsWidget::onExportAll);
  connect(dataFolderButton, &QPushButton::clicked, this,
          &SettingsWidget::onOpenDataFolder);
}

void SettingsWidget::updatePriceStatus() {
  if (api->arePricesLoaded()) {
    apiStatusLabel->setText(
        QString("✓ %1 items loaded").arg(api->priceCount()));
    apiStatusLabel->setStyleSheet("color: #6ec66e;");
    priceLastUpdatedLabel->setText(api->lastUpdated());
  } else {
    apiStatusLabel->setText("Not loaded");
    apiStatusLabel->setStyleSheet("color: #dc4646;");
    priceLastUpdatedLabel->setText("—");
  }
}

void SettingsWidget::onTestAPI() {
  apiStatusLabel->setText("Checking...");
  apiStatusLabel->setStyleSheet("color: #5b9bd5;");
  updatePriceStatus();
}

void SettingsWidget::onSaveSettings() {
  saveSettings();
  emit settingsChanged();
  QMessageBox::information(this, "Settings", "Settings saved.");
}

void SettingsWidget::loadSettings() {
  QSettings settings("CS2Vault", "Settings");
  autoUpdateCheckBox->setChecked(settings.value("autoUpdate", true).toBool());
  updateIntervalSpinBox->setValue(settings.value("updateInterval", 5).toInt());
  currencyComboBox->setCurrentText(
      settings.value("currency", "USD").toString());
  QString savedSource = settings.value("priceSource", "Skinport").toString();
  int idx = sourceComboBox->findText(savedSource);
  if (idx >= 0)
    sourceComboBox->setCurrentIndex(idx);
}

void SettingsWidget::saveSettings() {
  QSettings settings("CS2Vault", "Settings");
  settings.setValue("autoUpdate", autoUpdateCheckBox->isChecked());
  settings.setValue("updateInterval", updateIntervalSpinBox->value());
  settings.setValue("currency", currencyComboBox->currentText());
  settings.setValue("priceSource", sourceComboBox->currentText());
}

void SettingsWidget::onExportAll() {
  QString fileName = QFileDialog::getSaveFileName(
      this, "Export All Portfolios", "all_portfolios.csv", "CSV Files (*.csv)");
  if (fileName.isEmpty())
    return;

  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, "Error", "Could not open file for writing.");
    return;
  }

  QTextStream out(&file);
  out << "Portfolio,Skin Name,Condition,Float,Quantity,Buy Price,"
         "Current Price,Purchase Date,Notes\n";

  int totalItems = 0;
  for (const Portfolio &p : portfolioManager->getAllPortfolios()) {
    for (const PortfolioItem &item : p.items) {
      out << QString("\"%1\",\"%2\",%3,%4,%5,%6,%7,\"%8\",\"%9\"\n")
                 .arg(p.name)
                 .arg(item.skinName)
                 .arg(item.condition)
                 .arg(item.floatValue)
                 .arg(item.quantity)
                 .arg(item.buyPrice)
                 .arg(item.currentPrice)
                 .arg(item.purchaseDate)
                 .arg(item.notes);
      ++totalItems;
    }
  }

  file.close();
  QMessageBox::information(
      this, "Export Complete",
      QString("Exported %1 items across %2 portfolios.")
          .arg(totalItems)
          .arg(portfolioManager->getAllPortfolios().size()));
}

void SettingsWidget::onOpenDataFolder() {
  QString dataPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDesktopServices::openUrl(QUrl::fromLocalFile(dataPath));
}
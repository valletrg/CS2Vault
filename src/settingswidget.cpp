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
                               AccountManager *accountManager,
                               QWidget *parent)
    : QWidget(parent), api(api), portfolioManager(portfolioManager),
      accountManager(accountManager) {
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
  sourceComboBox->addItems({"white.market  (~1 hour)",
                             "white.market (fast)  (~10 min)",
                             "Loot.Farm  (~1 min)",
                             "Buff163  (8-24 hours)"});
  priceLayout->addRow("Price source:", sourceComboBox);

  connect(
      sourceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
      [this]() {
        static const QMap<QString, QString> urls = {
            {"white.market  (~1 hour)",
             "https://s3.white.market/export/v1/prices/730.json"},
            {"white.market (fast)  (~10 min)",
             "https://s3.white.market/export/v1/prices/730.10min.json"},
            {"Loot.Farm  (~1 min)", "https://loot.farm/fullprice.json"},
            {"Buff163  (8-24 hours)",
             "https://prices.csgotrader.app/latest/buff163.json"}};
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

  // ── Accounts group ────────────────────────────────────────────────────────
  auto *accountsGroup = new QGroupBox("Accounts", this);
  auto *accountsGroupLayout = new QVBoxLayout(accountsGroup);

  accountsListWidget = new QWidget(accountsGroup);
  auto *listLayout = new QVBoxLayout(accountsListWidget);
  listLayout->setContentsMargins(0, 0, 0, 0);
  listLayout->setSpacing(4);

  auto *addAccountButton = new QPushButton("Add Account", accountsGroup);
  addAccountButton->setFixedHeight(32);

  auto *accountsNote =
      new QLabel("Each account's refresh token is stored encrypted on disk.",
                 accountsGroup);
  accountsNote->setWordWrap(true);
  accountsNote->setStyleSheet("color: #666; font-size: 11px;");

  accountsGroupLayout->addWidget(accountsListWidget);
  accountsGroupLayout->addWidget(addAccountButton);
  accountsGroupLayout->addWidget(accountsNote);

  mainLayout->addWidget(accountsGroup);

  connect(addAccountButton, &QPushButton::clicked, this,
          &SettingsWidget::onAddAccount);

  if (accountManager) {
    connect(accountManager, &AccountManager::accountsChanged, this,
            &SettingsWidget::refreshAccountsList);
    refreshAccountsList();
  }

  // ── Trade Offers group ────────────────────────────────────────────────────
  auto *tradesGroup = new QGroupBox("Trade Offers", this);
  auto *tradesLayout = new QVBoxLayout(tradesGroup);

  auto *resetConsentBtn =
      new QPushButton("Reset trade offers consent", tradesGroup);
  resetConsentBtn->setFixedHeight(32);

  auto *resetConsentNote = new QLabel(
      "Resetting consent will show the consent screen again the next time "
      "you visit the Trades tab.",
      tradesGroup);
  resetConsentNote->setWordWrap(true);
  resetConsentNote->setStyleSheet("color: #666; font-size: 11px;");

  tradesLayout->addWidget(resetConsentBtn);
  tradesLayout->addWidget(resetConsentNote);
  mainLayout->addWidget(tradesGroup);

  connect(resetConsentBtn, &QPushButton::clicked, this, [this]() {
    emit tradesConsentReset();
    QMessageBox::information(this, "Consent Reset",
        "Consent reset. You will see the consent screen again next time "
        "you visit the Trades tab.");
  });

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
  QString savedSource = settings.value("priceSource", "white.market (fast)  (~10 min)").toString();
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

void SettingsWidget::refreshAccountsList() {
  auto *layout = qobject_cast<QVBoxLayout *>(accountsListWidget->layout());

  // Clear existing rows
  while (layout->count()) {
    auto *item = layout->takeAt(0);
    if (item->widget())
      item->widget()->deleteLater();
    delete item;
  }

  if (!accountManager)
    return;

  const auto accounts = accountManager->accounts();
  const QString activeId = accountManager->activeAccountId();

  if (accounts.isEmpty()) {
    auto *emptyLabel = new QLabel("No accounts saved.", accountsListWidget);
    emptyLabel->setStyleSheet("color: #666; font-size: 11px;");
    layout->addWidget(emptyLabel);
    return;
  }

  for (const auto &acc : accounts) {
    bool isActive = (acc.id == activeId);

    auto *row = new QWidget(accountsListWidget);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 2, 0, 2);
    rowLayout->setSpacing(8);

    auto *indicator = new QLabel(isActive ? "●" : "○", row);
    indicator->setFixedWidth(14);
    indicator->setStyleSheet(isActive ? "color: #6ec66e;" : "color: #555;");

    auto *nameCol = new QWidget(row);
    nameCol->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *nameColLayout = new QVBoxLayout(nameCol);
    nameColLayout->setContentsMargins(0, 0, 0, 0);
    nameColLayout->setSpacing(1);

    if (!acc.personaName.isEmpty()) {
      auto *nameLabel = new QLabel(acc.personaName, nameCol);
      nameLabel->setStyleSheet("color: #e0e0e0; font-size: 12px;");
      nameColLayout->addWidget(nameLabel);
    }

    auto *steamIdLabel = new QLabel(acc.steamId, nameCol);
    steamIdLabel->setStyleSheet("color: #666; font-size: 10px;");
    nameColLayout->addWidget(steamIdLabel);

    auto *switchBtn = new QPushButton("Switch", row);
    switchBtn->setFixedHeight(26);
    switchBtn->setMinimumWidth(64);
    switchBtn->setEnabled(!isActive);

    auto *removeBtn = new QPushButton("Remove", row);
    removeBtn->setFixedHeight(26);
    removeBtn->setMinimumWidth(64);
    removeBtn->setStyleSheet(
        "QPushButton { color: #dc4646; }"
        "QPushButton:hover { background: #3a1a1a; border-color: #dc4646; }");

    const QString id = acc.id;
    connect(switchBtn, &QPushButton::clicked, this,
            [this, id]() { onSwitchAccount(id); });
    connect(removeBtn, &QPushButton::clicked, this,
            [this, id]() { onRemoveAccount(id); });

    rowLayout->addWidget(indicator);
    rowLayout->addWidget(nameCol);
    rowLayout->addWidget(switchBtn);
    rowLayout->addWidget(removeBtn);

    layout->addWidget(row);
  }
}

void SettingsWidget::onAddAccount() { emit addAccountRequested(); }

void SettingsWidget::onSwitchAccount(const QString &id) {
  emit switchAccountRequested(id);
}

void SettingsWidget::onRemoveAccount(const QString &id) {
  if (!accountManager)
    return;

  auto acc = accountManager->account(id);
  QString name = acc.personaName.isEmpty() ? acc.steamId : acc.personaName;

  auto reply = QMessageBox::question(
      this, "Remove Account",
      QString("Remove \"%1\"? The saved token will be deleted.").arg(name),
      QMessageBox::Yes | QMessageBox::Cancel);

  if (reply == QMessageBox::Yes)
    accountManager->removeAccount(id);
}
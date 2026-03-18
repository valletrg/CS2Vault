
#include "settingswidget.h"
#include "priceempireapi.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QSettings>
#include <QApplication>
#include <QTimer>

SettingsWidget::SettingsWidget(PriceEmpireAPI *api, QWidget *parent)
    : QWidget(parent)
    , api(api)
{
    setupUI();
    loadSettings();
}

SettingsWidget::~SettingsWidget()
{
    saveSettings();
}

void SettingsWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // API Status Group
    QGroupBox *apiGroup = new QGroupBox("API Status", this);
    QFormLayout *apiLayout = new QFormLayout(apiGroup);
    
    apiStatusLabel = new QLabel("Checking...", apiGroup);
    testAPIButton = new QPushButton("Test Connection", apiGroup);
    
    apiLayout->addRow("Status:", apiStatusLabel);
    apiLayout->addRow("", testAPIButton);
    
    mainLayout->addWidget(apiGroup);
    
    // App Settings
    QGroupBox *appGroup = new QGroupBox("Application Settings", this);
    QFormLayout *appLayout = new QFormLayout(appGroup);
    
    autoUpdateCheckBox = new QCheckBox("Auto-update prices", appGroup);
    autoUpdateCheckBox->setChecked(true);
    
    updateIntervalSpinBox = new QSpinBox(appGroup);
    updateIntervalSpinBox->setRange(1, 60);
    updateIntervalSpinBox->setValue(5);
    updateIntervalSpinBox->setSuffix(" minutes");
    
    currencyComboBox = new QComboBox(appGroup);
    currencyComboBox->addItems({"USD", "EUR", "GBP", "CAD", "AUD"});
    
    appLayout->addRow(autoUpdateCheckBox);
    appLayout->addRow("Update Interval:", updateIntervalSpinBox);
    appLayout->addRow("Currency:", currencyComboBox);
    
    mainLayout->addWidget(appGroup);
    
    // Save button
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    saveButton = new QPushButton("Save Settings", this);
    buttonLayout->addStretch();
    buttonLayout->addWidget(saveButton);
    
    mainLayout->addLayout(buttonLayout);
    mainLayout->addStretch();
    
    connect(testAPIButton, &QPushButton::clicked, this, &SettingsWidget::onTestAPI);
    connect(saveButton, &QPushButton::clicked, this, &SettingsWidget::onSaveSettings);
    
    // Check API status on startup
    QTimer::singleShot(500, this, &SettingsWidget::onTestAPI);
}

void SettingsWidget::loadSettings()
{
    QSettings settings("CS2Trader", "Settings");
    
    autoUpdateCheckBox->setChecked(settings.value("autoUpdate", true).toBool());
    updateIntervalSpinBox->setValue(settings.value("updateInterval", 5).toInt());
    currencyComboBox->setCurrentText(settings.value("currency", "USD").toString());
}

void SettingsWidget::saveSettings()
{
    QSettings settings("CS2Trader", "Settings");
    
    settings.setValue("autoUpdate", autoUpdateCheckBox->isChecked());
    settings.setValue("updateInterval", updateIntervalSpinBox->value());
    settings.setValue("currency", currencyComboBox->currentText());
}

void SettingsWidget::onTestAPI()
{
    apiStatusLabel->setText("Testing connection...");
    apiStatusLabel->setStyleSheet("color: blue;");
    QApplication::processEvents();
    
    if (api->testConnection()) {
        apiStatusLabel->setText("Connected to PriceEmpire API!");
        apiStatusLabel->setStyleSheet("color: green;");
    } else {
        apiStatusLabel->setText("API connection failed. Check your key.");
        apiStatusLabel->setStyleSheet("color: red;");
    }
}

void SettingsWidget::onSaveSettings()
{
    saveSettings();
    emit settingsChanged();
    QMessageBox::information(this, "Settings", "Settings saved successfully!");
}
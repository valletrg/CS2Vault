#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

#include "accountmanager.h"
#include "priceempireapi.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

class PortfolioManager;

class SettingsWidget : public QWidget {
  Q_OBJECT

public:
  explicit SettingsWidget(PriceEmpireAPI *api,
                          PortfolioManager *portfolioManager,
                          AccountManager *accountManager,
                          QWidget *parent = nullptr);
  ~SettingsWidget();

  void updatePriceStatus();

signals:
  void settingsChanged();
  void addAccountRequested();
  void switchAccountRequested(const QString &id);

private slots:
  void onSaveSettings();
  void onTestAPI();
  void onExportAll();
  void onOpenDataFolder();
  void onAddAccount();
  void onRemoveAccount(const QString &id);
  void onSwitchAccount(const QString &id);
  void refreshAccountsList();

private:
  void setupUI();
  void loadSettings();
  void saveSettings();

  PriceEmpireAPI *api;
  PortfolioManager *portfolioManager;
  AccountManager *accountManager = nullptr;

  QLabel *apiStatusLabel;
  QLabel *priceLastUpdatedLabel;
  QPushButton *testAPIButton;

  QCheckBox *autoUpdateCheckBox;
  QSpinBox *updateIntervalSpinBox;
  QComboBox *currencyComboBox;
  QComboBox *sourceComboBox = nullptr;

  QWidget *accountsListWidget = nullptr;

  QPushButton *saveButton;
};

#endif // SETTINGSWIDGET_H
#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

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
                          QWidget *parent = nullptr);
  ~SettingsWidget();

  void updatePriceStatus();

signals:
  void settingsChanged();

private slots:
  void onSaveSettings();
  void onTestAPI();
  void onExportAll();
  void onOpenDataFolder();

private:
  void setupUI();
  void loadSettings();
  void saveSettings();

  PriceEmpireAPI *api;
  PortfolioManager *portfolioManager;

  QLabel *apiStatusLabel;
  QLabel *priceLastUpdatedLabel;
  QPushButton *testAPIButton;

  QCheckBox *autoUpdateCheckBox;
  QSpinBox *updateIntervalSpinBox;
  QComboBox *currencyComboBox;
  QComboBox *sourceComboBox = nullptr;

  QPushButton *saveButton;
};

#endif // SETTINGSWIDGET_H
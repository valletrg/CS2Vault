
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "steamcompanion.h"
#include <QCloseEvent>
#include <QMainWindow>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QTimer>
#include <memory>


class QTabWidget;

class StorageUnitWidget;
class PortfolioWidget;
class SettingsWidget;
class PriceEmpireAPI;
class SteamAPI;
class PortfolioManager;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(SteamCompanion *companion, QWidget *parent = nullptr);
  ~MainWindow() override;

private slots:
  void updatePrices();
  void showNotification(const QString &title, const QString &message);
  void onAboutClicked();

private:
  void setupUI();
  void setupConnections();
  void setupSystemTray();
  void loadSettings();
  void saveSettings();
  void closeEvent(QCloseEvent *event) override;

  QTabWidget *tabWidget = nullptr;

  StorageUnitWidget *storageUnitWidget = nullptr;
  PortfolioWidget *portfolioWidget = nullptr;
  SettingsWidget *settingsWidget = nullptr;

  PriceEmpireAPI *api = nullptr;
  SteamAPI *steamApi = nullptr;
  PortfolioManager *portfolioManager = nullptr;

  QTimer *priceUpdateTimer = nullptr;
  QSystemTrayIcon *trayIcon = nullptr;
  QMenu *trayMenu = nullptr;
  SteamCompanion *steamCompanion = nullptr;
};

#endif // MAINWINDOW_H

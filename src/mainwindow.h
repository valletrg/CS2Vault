#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "accountmanager.h"
#include "itemdatabase.h"
#include "steamcompanion.h"
#include "updatechecker.h"
#include <QCloseEvent>
#include <QMainWindow>
#include <QMenu>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

class QPushButton;
class QLabel;
class DashboardWidget;
class StorageUnitWidget;
class PortfolioWidget;
class SettingsWidget;
class WatchlistWidget;
class PriceEmpireAPI;
class SteamAPI;
class PortfolioManager;
class WatchlistManager;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(SteamCompanion *companion, AccountManager *accountManager,
                      QWidget *parent = nullptr);
  ~MainWindow() override;

private slots:
  void updatePrices();
  void showNotification(const QString &title, const QString &message);
  void onAboutClicked();
  void switchToPage(int index);
  void onSwitchAccountRequested(const QString &id);
  void onAddAccountRequested();

private:
  void setupUI();
  void setupConnections();
  void setupSystemTray();
  void loadSettings();
  void saveSettings();
  void closeEvent(QCloseEvent *event) override;

  QStackedWidget *stackedWidget = nullptr;
  QWidget *sidebar = nullptr;
  QList<QPushButton *> navButtons;

  DashboardWidget *dashboardWidget = nullptr;
  StorageUnitWidget *storageUnitWidget = nullptr;
  PortfolioWidget *portfolioWidget = nullptr;
  SettingsWidget *settingsWidget = nullptr;
  WatchlistWidget *watchlistWidget = nullptr;

  AccountManager *accountManager = nullptr;
  PriceEmpireAPI *api = nullptr;
  SteamAPI *steamApi = nullptr;
  PortfolioManager *portfolioManager = nullptr;
  WatchlistManager *watchlistManager = nullptr;
  ItemDatabase *itemDb = nullptr;

  QTimer *priceUpdateTimer = nullptr;
  QSystemTrayIcon *trayIcon = nullptr;
  QMenu *trayMenu = nullptr;
  SteamCompanion *steamCompanion = nullptr;
  UpdateChecker *updateChecker = nullptr;
};

#endif // MAINWINDOW_H
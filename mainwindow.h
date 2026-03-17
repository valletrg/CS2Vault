
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QMenu>
#include <QCloseEvent>
#include <memory>

class QTabWidget;

class CalculatorWidget;
class PortfolioWidget;
class SettingsWidget;
class PriceEmpireAPI;
class SteamAPI;
class PortfolioManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
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
    
    CalculatorWidget *calculatorWidget = nullptr;
    PortfolioWidget *portfolioWidget = nullptr;
    SettingsWidget *settingsWidget = nullptr;
    
    PriceEmpireAPI *api = nullptr;
    SteamAPI *steamApi = nullptr;
    PortfolioManager *portfolioManager = nullptr;
    
    QTimer *priceUpdateTimer = nullptr;
    QSystemTrayIcon *trayIcon = nullptr;
    QMenu *trayMenu = nullptr;
};

#endif // MAINWINDOW_H

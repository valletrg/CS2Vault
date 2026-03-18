#include "mainwindow.h"
#include "portfoliowidget.h"
#include "settingswidget.h"
#include "priceempireapi.h"
#include "steamapi.h"
#include "portfoliomanager.h"
#include "storageunitwidget.h"

#include <QTabWidget>
#include <QMessageBox>
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QApplication>
#include <QSettings>
#include <QCloseEvent>

MainWindow::MainWindow(SteamCompanion *companion, QWidget *parent)
    : QMainWindow(parent)
    , steamCompanion(companion)
    , tabWidget(new QTabWidget(this))
    , api(new PriceEmpireAPI(this))
    , steamApi(new SteamAPI(this))
    , portfolioManager(new PortfolioManager(this))
    , priceUpdateTimer(new QTimer(this))
{
    steamCompanion->setParent(this); // takes ownership
    setupUI();
    setupConnections();
    setupSystemTray();
    loadSettings();
    
    connect(priceUpdateTimer, &QTimer::timeout, this, &MainWindow::updatePrices);
    priceUpdateTimer->start(5 * 60 * 1000);
    
    statusBar()->showMessage("Welcome to CS2 Skin Trader!", 5000);
}

MainWindow::~MainWindow()
{
    saveSettings();
}

void MainWindow::setupUI()
{
    setWindowTitle("CS2Vault");
    setMinimumSize(1100, 750);
    
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    storageUnitWidget = new StorageUnitWidget(steamCompanion, this);
    portfolioWidget = new PortfolioWidget(api, steamApi, portfolioManager, steamCompanion, this);
    settingsWidget = new SettingsWidget(api, this);
    

    tabWidget->addTab(storageUnitWidget, "Storage Units");
    tabWidget->addTab(portfolioWidget, "Portfolio");
    tabWidget->addTab(settingsWidget, "Settings");
    
    mainLayout->addWidget(tabWidget);
    setCentralWidget(centralWidget);
    
    QMenu *fileMenu = menuBar()->addMenu("&File");
    QAction *exitAction = fileMenu->addAction("E&xit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);
    
    QMenu *toolsMenu = menuBar()->addMenu("&Tools");
    QAction *updateAction = toolsMenu->addAction("&Update Prices");
    updateAction->setShortcut(QKeySequence::Refresh);
    connect(updateAction, &QAction::triggered, this, &MainWindow::updatePrices);
    
    QMenu *helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About", this, &MainWindow::onAboutClicked);
    helpMenu->addAction("About &Qt", qApp, &QApplication::aboutQt);
    
    statusBar()->addPermanentWidget(new QLabel("Ready", this));
}

void MainWindow::setupConnections()
{
    connect(settingsWidget, &SettingsWidget::settingsChanged,
        this, &MainWindow::loadSettings);

    connect(portfolioWidget, &PortfolioWidget::portfolioUpdated,
            this, [this]() {
        statusBar()->showMessage("Portfolio updated", 3000);
    });
}

void MainWindow::setupSystemTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    
    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setToolTip("CS2Vault");
    
    trayMenu = new QMenu(this);
    trayMenu->addAction("Show", this, &QWidget::showNormal);
    trayMenu->addAction("Hide", this, &QWidget::hide);
    trayMenu->addSeparator();
    trayMenu->addAction("Exit", qApp, &QApplication::quit);
    
    trayIcon->setContextMenu(trayMenu);
    trayIcon->show();
}

void MainWindow::loadSettings()
{
    QSettings settings("CS2Vault", "Settings");

    QByteArray geometry = settings.value("geometry").toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    int interval = settings.value("updateInterval", 5).toInt();
    priceUpdateTimer->setInterval(interval * 60 * 1000);

    if (settings.value("autoUpdate", true).toBool())
        priceUpdateTimer->start();
    else
        priceUpdateTimer->stop();
}

void MainWindow::saveSettings()
{
    QSettings settings("CS2Vault", "Settings");
    settings.setValue("geometry", saveGeometry());
}

void MainWindow::updatePrices()
{
    QSettings settings("CS2Vault", "Settings");
    if (!settings.value("autoUpdate", true).toBool()) return;

    if (!api->isValid()) {
        statusBar()->showMessage("API not configured", 5000);
        return;
    }

    statusBar()->showMessage("Updating prices...", 2000);
    portfolioWidget->updateAllPrices();
}

void MainWindow::showNotification(const QString &title, const QString &message)
{
    if (trayIcon && trayIcon->isVisible()) {
        trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 3000);
    }
}

void MainWindow::onAboutClicked()
{
QMessageBox::about(this, "About CS2Vault",
    "<h3>CS2Vault v1.0</h3>"
    "<p>A CS2 inventory manager and portfolio tracker for Linux.</p>"
    "<ul>"
    "<li>Steam inventory import via GC</li>"
    "<li>Storage unit management</li>"
    "<li>Portfolio tracking with price history</li>"
    "<li>Steam Market price checking</li>"
    "</ul>");
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (trayIcon && trayIcon->isVisible()) {
        hide();
        event->ignore();
    } else {
        event->accept();
    }
}

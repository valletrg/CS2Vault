#include "mainwindow.h"
#include "calculatorwidget.h"
#include "portfoliowidget.h"
#include "settingswidget.h"
#include "priceempireapi.h"
#include "steamapi.h"
#include "portfoliomanager.h"

#include <QTabWidget>
#include <QMessageBox>
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QApplication>
#include <QSettings>
#include <QCloseEvent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , tabWidget(new QTabWidget(this))
    , api(new PriceEmpireAPI(this))
    , steamApi(new SteamAPI(this))
    , portfolioManager(new PortfolioManager(this))
    , priceUpdateTimer(new QTimer(this))
    , trayIcon(nullptr)
    , trayMenu(nullptr)
{
    setupUI();
    setupConnections();
    setupSystemTray();
    loadSettings();
    
    connect(priceUpdateTimer, &QTimer::timeout, this, &MainWindow::updatePrices);
    priceUpdateTimer->start(5 * 60 * 1000);
    
    statusBar()->showMessage("Welcome to CS2 Skin Trader Calculator!", 5000);
}

MainWindow::~MainWindow()
{
    saveSettings();
}

void MainWindow::setupUI()
{
    setWindowTitle("CS2 Skin Trader Calculator v2.0");
    setMinimumSize(1100, 750);
    
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    
    calculatorWidget = new CalculatorWidget(api, this);
    portfolioWidget = new PortfolioWidget(api, steamApi, portfolioManager, this);
    settingsWidget = new SettingsWidget(api, this);
    
    tabWidget->addTab(calculatorWidget, "Calculators");
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
    trayIcon->setToolTip("CS2 Skin Trader Calculator");
    
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
    QSettings settings("CS2Trader", "Settings");
    
    QByteArray geometry = settings.value("geometry").toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
}

void MainWindow::saveSettings()
{
    QSettings settings("CS2Trader", "Settings");
    settings.setValue("geometry", saveGeometry());
}

void MainWindow::updatePrices()
{
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
    QMessageBox::about(this, "About CS2 Skin Trader Calculator",
        "<h3>CS2 Skin Trader Calculator v2.0</h3>"
        "<p>A comprehensive tool for CS2 skin traders.</p>"
        "<ul>"
        "<li>Profit/Loss calculations</li>"
        "<li>Trade-up analyzer</li>"
        "<li>Portfolio tracking with Steam import</li>"
        "<li>Multiple portfolio support</li>"
        "<li>CSV import/export</li>"
        "<li>PriceEmpire integration</li>"
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

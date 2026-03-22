#include "mainwindow.h"
#include "dashboardwidget.h"
#include "loginwindow.h"
#include "portfoliomanager.h"
#include "portfoliowidget.h"
#include "priceempireapi.h"
#include "settingswidget.h"
#include "steamapi.h"
#include "storageunitwidget.h"
#include "watchlistmanager.h"
#include "watchlistwidget.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSvgRenderer>
#include <QVBoxLayout>

MainWindow::MainWindow(SteamCompanion *companion, AccountManager *accountManager,
                       QWidget *parent)
    : QMainWindow(parent), steamCompanion(companion),
      accountManager(accountManager),
      api(new PriceEmpireAPI(this)), steamApi(new SteamAPI(this)),
      portfolioManager(new PortfolioManager(this)),
      watchlistManager(new WatchlistManager(this)),
      priceUpdateTimer(new QTimer(this)) {
  steamCompanion->setParent(this);
  setupUI();
  setupConnections();
  setupSystemTray();
  loadSettings();

  connect(priceUpdateTimer, &QTimer::timeout, this, &MainWindow::updatePrices);
  priceUpdateTimer->start(5 * 60 * 1000);

  connect(api, &PriceEmpireAPI::pricesLoaded, this, [this]() {
    statusBar()->showMessage("Prices loaded — ready.", 5000);
    if (watchlistWidget)
      watchlistWidget->updateAllPrices();
  });
  connect(api, &PriceEmpireAPI::pricesError, this, [this](const QString &err) {
    statusBar()->showMessage("Price fetch failed: " + err, 0);
  });

  QSettings settings("CS2Vault", "Settings");
  QString savedSource = settings.value("priceSource", "Skinport").toString();
  static const QMap<QString, QString> urls = {
      {"Skinport", "https://fursense.lol/prices.json"},
      {"white.market", "https://fursense.lol/prices-whitemarket.json"},
      {"market.csgo.com", "https://fursense.lol/prices-marketcsgo.json"}};
  api->initSourceUrl(
      urls.value(savedSource, "https://fursense.lol/prices.json"));
  api->loadPrices();

  itemDb = new ItemDatabase(this);
  connect(itemDb, &ItemDatabase::loaded, this, [this]() {
    statusBar()->showMessage(
        QString("Item database loaded — %1 items.").arg(itemDb->itemCount()),
        3000);
  });
  connect(itemDb, &ItemDatabase::error, this, [this](const QString &err) {
    qWarning() << "ItemDatabase error:" << err;
  });
  itemDb->load();

  statusBar()->showMessage("Loading prices...", 0);
}

MainWindow::~MainWindow() { saveSettings(); }

void MainWindow::setupUI() {
  setWindowTitle("CS2Vault");
  setMinimumSize(1100, 750);

  // ── Central widget with sidebar + content ─────────────────────────────────
  QWidget *central = new QWidget(this);
  QHBoxLayout *rootLayout = new QHBoxLayout(central);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  // ── Sidebar ───────────────────────────────────────────────────────────────
  sidebar = new QWidget(central);
  sidebar->setObjectName("sidebar");
  sidebar->setFixedWidth(200);

  QVBoxLayout *sideLayout = new QVBoxLayout(sidebar);
  sideLayout->setContentsMargins(0, 0, 0, 0);
  sideLayout->setSpacing(0);

  // Logo / app name area
  QLabel *logoLabel = new QLabel("CS2VAULT", sidebar);
  logoLabel->setObjectName("sidebarLogo");
  logoLabel->setAlignment(Qt::AlignCenter);
  logoLabel->setFixedHeight(80);
  sideLayout->addWidget(logoLabel);

  // Nav buttons — unicode icons + label
  struct NavItem {
    QString iconPath;
    QString label;
  };
  QList<NavItem> items = {
      {":/icons/dashboard.svg", "Dashboard"},
      {":/icons/storage.svg", "Storage Units"},
      {":/icons/portfolio.svg", "Portfolio"},
      {":/icons/watchlist.svg", "Watchlist"},
      {":/icons/settings.svg", "Settings"},
  };

  for (int i = 0; i < items.size(); ++i) {
    QPushButton *btn = new QPushButton(sidebar);
    btn->setObjectName("navButton");
    btn->setCheckable(true);
    btn->setFixedHeight(52);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setText(items[i].label);

    QSvgRenderer renderer(items[i].iconPath);
    if (renderer.isValid()) {
      QPixmap px(18, 18);
      px.fill(Qt::transparent);
      QPainter painter(&px);
      renderer.render(&painter);
      painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
      painter.fillRect(px.rect(), QColor("#4fc3f7"));
      painter.end();
      btn->setIcon(QIcon(px));
      btn->setIconSize(QSize(18, 18));
    }

    btn->setStyleSheet("text-align: left; padding-left: 16px;");
    connect(btn, &QPushButton::clicked, this, [this, i]() { switchToPage(i); });
    sideLayout->addWidget(btn);
    navButtons.append(btn);
  }

  sideLayout->addStretch();

  // Version label at bottom of sidebar
  QLabel *versionLabel = new QLabel("v1.2.0", sidebar);
  versionLabel->setObjectName("sidebarVersion");
  versionLabel->setAlignment(Qt::AlignCenter);
  versionLabel->setFixedHeight(32);
  sideLayout->addWidget(versionLabel);

  updateChecker = new UpdateChecker(this);

  // Update checker
  connect(updateChecker, &UpdateChecker::updateAvailable, this,
          [this](const QString &latest, const QString &message) {
            // Show a non-intrusive banner in the status bar
            QString text =
                QString("Update available: v%1 — github.com/valletrg/CS2Vault")
                    .arg(latest);
            if (!message.isEmpty())
              text += " — " + message;
            statusBar()->showMessage(text, 0); // 0 = stays permanently
          });

  connect(updateChecker, &UpdateChecker::upToDate, this,
          []() { qInfo() << "CS2Vault is up to date."; });

  QTimer::singleShot(3000, updateChecker, &UpdateChecker::check);

  // ── Stacked content area ──────────────────────────────────────────────────
  stackedWidget = new QStackedWidget(central);
  stackedWidget->setObjectName("contentArea");

  dashboardWidget = new DashboardWidget(steamCompanion, api, steamApi,
                                        accountManager, this);
  storageUnitWidget = new StorageUnitWidget(steamCompanion, api, this);
  portfolioWidget = new PortfolioWidget(api, steamApi, portfolioManager,
                                        steamCompanion, this);
  watchlistWidget = new WatchlistWidget(api, watchlistManager, this);
  settingsWidget = new SettingsWidget(api, portfolioManager, accountManager, this);

  stackedWidget->addWidget(dashboardWidget);
  stackedWidget->addWidget(storageUnitWidget);
  stackedWidget->addWidget(portfolioWidget);
  stackedWidget->addWidget(watchlistWidget);
  stackedWidget->addWidget(settingsWidget);

  rootLayout->addWidget(sidebar);
  rootLayout->addWidget(stackedWidget, 1);

  setCentralWidget(central);

  // Select first page
  switchToPage(0);

  // ── Menu bar ──────────────────────────────────────────────────────────────
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

void MainWindow::switchToPage(int index) {
  stackedWidget->setCurrentIndex(index);
  for (int i = 0; i < navButtons.size(); ++i)
    navButtons[i]->setChecked(i == index);
}

void MainWindow::setupConnections() {
  connect(settingsWidget, &SettingsWidget::settingsChanged, this,
          &MainWindow::loadSettings);
  connect(portfolioWidget, &PortfolioWidget::portfolioUpdated, this,
          [this]() { statusBar()->showMessage("Portfolio updated", 3000); });
  connect(settingsWidget, &SettingsWidget::switchAccountRequested, this,
          &MainWindow::onSwitchAccountRequested);
  connect(settingsWidget, &SettingsWidget::addAccountRequested, this,
          &MainWindow::onAddAccountRequested);
  connect(watchlistWidget, &WatchlistWidget::watchlistUpdated, this,
          [this]() { statusBar()->showMessage("Watchlist updated", 3000); });
}

void MainWindow::setupSystemTray() {
  if (!QSystemTrayIcon::isSystemTrayAvailable())
    return;

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

void MainWindow::loadSettings() {
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

void MainWindow::saveSettings() {
  QSettings settings("CS2Vault", "Settings");
  settings.setValue("geometry", saveGeometry());
}

void MainWindow::updatePrices() {
  QSettings settings("CS2Vault", "Settings");
  if (!settings.value("autoUpdate", true).toBool())
    return;
  statusBar()->showMessage("Refreshing prices...", 0);
  api->reloadPrices();
}

void MainWindow::showNotification(const QString &title,
                                  const QString &message) {
  if (trayIcon && trayIcon->isVisible())
    trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 3000);
}

void MainWindow::onAboutClicked() {
  QMessageBox::about(
      this, "About CS2Vault",
      "<h3>CS2Vault v1.2</h3>"
      "<p>A CS2 inventory manager and portfolio tracker for Linux.</p>"
      "<ul>"
      "<li>Steam inventory import via GC</li>"
      "<li>Storage unit management</li>"
      "<li>Portfolio tracking with price history</li>"
      "<li>Steam Market price checking</li>"
      "</ul>");
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (trayIcon && trayIcon->isVisible()) {
    hide();
    event->ignore();
  } else {
    event->accept();
  }
}

void MainWindow::onSwitchAccountRequested(const QString &id) {
  if (!accountManager || id == accountManager->activeAccountId())
    return;

  accountManager->setActiveAccount(id);

  QString profileDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/accounts/" + id;

  steamCompanion->stop();
  steamCompanion->start(profileDir);

  statusBar()->showMessage("Switching account...", 0);
}

void MainWindow::onAddAccountRequested() {
  auto *loginWin = new LoginWindow();
  loginWin->setAttribute(Qt::WA_DeleteOnClose);

  connect(loginWin, &LoginWindow::loginComplete, this,
          [this, loginWin]() {
            // The new companion is discarded here; its Node.js process saved a
            // token in the default profile directory. A future step should call
            // AccountManager::addAccount() with the steamId and token once the
            // companion exposes them, then restart with the new profile path.
            loginWin->takeCompanion()->deleteLater();
            loginWin->close();
            QMessageBox::information(
                this, "Account Added",
                "Login successful. Restart CS2Vault to switch to the new "
                "account, or use Switch in Settings if it appears in the list.");
          });

  loginWin->show();
}

#include "dashboardwidget.h"

#include <QDateTime>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QVBoxLayout>

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

DashboardWidget::DashboardWidget(SteamCompanion *companion, PriceEmpireAPI *api,
                                 SteamAPI *steamApi,
                                 AccountManager *accountManager,
                                 QWidget *parent)
    : QWidget(parent), companion(companion), api(api), steamApi(steamApi),
      accountManager(accountManager),
      nam(new QNetworkAccessManager(this)) {
  setupUI();

  // ── Load cache before signals so data is visible immediately ─────────────
  loadCache();

  // ── Connections ──────────────────────────────────────────────────────────
  connect(companion, &SteamCompanion::gcReady, this,
          [this]() { this->companion->requestInventory(); });
  connect(companion, &SteamCompanion::inventoryReceived, this,
          &DashboardWidget::onInventoryReceived);
  connect(companion, &SteamCompanion::storageUnitReceived, this,
          &DashboardWidget::onStorageUnitReceived);
  connect(api, &PriceEmpireAPI::pricesLoaded, this,
          &DashboardWidget::onPricesLoaded);
  connect(steamApi, &SteamAPI::loginSuccessful, this,
          &DashboardWidget::onProfileLoaded);
  connect(companion, &SteamCompanion::loggedIn, this,
          [this](const QString &steamId) {
            this->steamApi->loginWithSteamId(steamId);
          });

  // If GC is already ready by the time this widget is created (e.g. account
  // switch), request inventory immediately rather than waiting for gcReady.
  if (companion->isGCReady())
    companion->requestInventory();

  // Kick off the profile fetch if we already know the active account.
  SteamAccount active = accountManager->activeAccount();
  if (!active.steamId.isEmpty()) {
    updateGreeting(active.personaName.isEmpty() ? active.steamId
                                                : active.personaName);
    steamApi->loginWithSteamId(active.steamId);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// UI setup
// ─────────────────────────────────────────────────────────────────────────────

QFrame *DashboardWidget::createValueCard(const QString &title,
                                         QLabel **valueLabel,
                                         QLabel **countLabel) {
  auto *card = new QFrame(this);
  card->setStyleSheet(
      "QFrame { background: #0f1923; border: 1px solid #1e2433; "
      "border-radius: 8px; }");

  auto *layout = new QVBoxLayout(card);
  layout->setContentsMargins(14, 10, 14, 10);
  layout->setSpacing(3);

  auto *titleLbl = new QLabel(title, card);
  titleLbl->setStyleSheet("color: #94a3b8; font-size: 11px; border: none; "
                          "background: transparent;");
  layout->addWidget(titleLbl);

  *valueLabel = new QLabel("—", card);
  (*valueLabel)
      ->setStyleSheet("color: #4fc3f7; font-size: 18px; border: none; "
                      "background: transparent; font-family: 'JetBrains Mono';");
  layout->addWidget(*valueLabel);

  *countLabel = new QLabel("", card);
  (*countLabel)
      ->setStyleSheet("color: #64748b; font-size: 10px; border: none; "
                      "background: transparent;");
  layout->addWidget(*countLabel);

  return card;
}

void DashboardWidget::setupUI() {
  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);

  auto *content = new QWidget();
  auto *mainLayout = new QVBoxLayout(content);
  mainLayout->setContentsMargins(32, 28, 32, 28);
  mainLayout->setSpacing(20);

  // ── Header: avatar + greeting ───────────────────────────────────────────
  auto *headerLayout = new QHBoxLayout();
  headerLayout->setSpacing(16);

  avatarLabel = new QLabel(content);
  avatarLabel->setFixedSize(64, 64);
  avatarLabel->setStyleSheet(
      "background: #1e2433; border-radius: 32px; color: #475569; "
      "font-size: 24px;");
  avatarLabel->setAlignment(Qt::AlignCenter);
  avatarLabel->setText("?");
  headerLayout->addWidget(avatarLabel);

  auto *nameColumn = new QVBoxLayout();
  nameColumn->setSpacing(2);

  greetingLabel = new QLabel("Good evening.", content);
  QFont greetFont;
  greetFont.setPointSize(20);
  greetFont.setBold(true);
  greetingLabel->setFont(greetFont);
  greetingLabel->setStyleSheet("color: #e2e8f0;");
  nameColumn->addWidget(greetingLabel);

  steamIdLabel = new QLabel("", content);
  steamIdLabel->setStyleSheet("color: #64748b; font-size: 11px;");
  nameColumn->addWidget(steamIdLabel);

  headerLayout->addLayout(nameColumn);
  headerLayout->addStretch();

  auto *refreshBtn = new QPushButton("⟳  Refresh", content);
  refreshBtn->setFixedHeight(32);
  refreshBtn->setStyleSheet(
      "QPushButton { background: #1e2433; color: #94a3b8; border: 1px solid "
      "#2a3347; border-radius: 6px; padding: 0 14px; font-size: 12px; }"
      "QPushButton:hover { background: #252d3d; color: #e2e8f0; }");
  connect(refreshBtn, &QPushButton::clicked, this,
          [this]() { companion->requestInventory(); });
  headerLayout->addWidget(refreshBtn);

  mainLayout->addLayout(headerLayout);

  // ── Value cards ─────────────────────────────────────────────────────────
  auto *cardsLayout = new QHBoxLayout();
  cardsLayout->setSpacing(12);

  cardsLayout->addWidget(
      createValueCard("Total", &totalValueLabel, &totalCountLabel));
  cardsLayout->addWidget(
      createValueCard("Storage Units", &storageValueLabel, &storageCountLabel));
  cardsLayout->addWidget(
      createValueCard("Inventory", &inventoryValueLabel, &inventoryCountLabel));

  mainLayout->addLayout(cardsLayout);

  // ── Top items table ─────────────────────────────────────────────────────
  auto *tableHeader = new QLabel("Most Valuable Items", content);
  tableHeader->setStyleSheet(
      "color: #94a3b8; font-size: 12px; text-transform: uppercase; "
      "letter-spacing: 1px;");
  mainLayout->addWidget(tableHeader);

  topItemsTable = new QTableWidget(0, 3, content);
  topItemsTable->setHorizontalHeaderLabels({"Name", "Source", "Price"});
  topItemsTable->horizontalHeader()->setStretchLastSection(true);
  topItemsTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  topItemsTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  topItemsTable->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  topItemsTable->verticalHeader()->setVisible(false);
  topItemsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  topItemsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  topItemsTable->setAlternatingRowColors(true);
  topItemsTable->setShowGrid(false);
  topItemsTable->setMinimumHeight(300);

  mainLayout->addWidget(topItemsTable, 1);

  // ── Status line ─────────────────────────────────────────────────────────
  statusLabel = new QLabel("Waiting for data...", content);
  statusLabel->setStyleSheet("color: #475569; font-size: 11px;");
  mainLayout->addWidget(statusLabel);

  scroll->setWidget(content);

  auto *outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->addWidget(scroll);
}

// ─────────────────────────────────────────────────────────────────────────────
// Profile
// ─────────────────────────────────────────────────────────────────────────────

void DashboardWidget::onProfileLoaded(const SteamProfile &profile) {
  updateGreeting(profile.personaName);
  steamIdLabel->setText(profile.steamId);

  if (!profile.avatarUrl.isEmpty())
    fetchAvatar(profile.avatarUrl);

  // Also update the account metadata so it persists
  SteamAccount active = accountManager->activeAccount();
  if (!active.id.isEmpty())
    accountManager->updateAccountMeta(active.id, profile.personaName,
                                      profile.avatarUrl);
}

void DashboardWidget::updateGreeting(const QString &name) {
  int hour = QTime::currentTime().hour();
  QString tod;
  if (hour < 12)
    tod = "Good morning";
  else if (hour < 18)
    tod = "Good afternoon";
  else
    tod = "Good evening";

  greetingLabel->setText(
      QString("%1, %2.").arg(tod, name.isEmpty() ? "Trader" : name));
}

void DashboardWidget::fetchAvatar(const QString &url) {
  auto *reply = nam->get(QNetworkRequest(QUrl(url)));
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
      return;

    QPixmap raw;
    raw.loadFromData(reply->readAll());
    if (raw.isNull())
      return;

    // Clip to circle
    int sz = 64;
    QPixmap scaled =
        raw.scaled(sz, sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap circular(sz, sz);
    circular.fill(Qt::transparent);

    QPainter painter(&circular);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addEllipse(0, 0, sz, sz);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, scaled);
    painter.end();

    avatarLabel->setPixmap(circular);
    avatarLabel->setText("");
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// Inventory data
// ─────────────────────────────────────────────────────────────────────────────

void DashboardWidget::onInventoryReceived(
    const QList<GCItem> &items, const QList<GCContainer> &ctrs) {

  // Only overwrite inventory items when a real inventory payload arrives.
  // gc_inventory_loaded also emits this signal with an empty items list (just
  // containers) — don't let that wipe out the actual inventory we already have.
  if (!items.isEmpty())
    inventoryItems = items;

  // Merge containers — the first signal may carry only containers, later
  // ones may carry items+containers. Keep the superset.
  if (!ctrs.isEmpty()) {
    containers = ctrs;
    containerNames.clear();
    for (const auto &c : containers)
      containerNames[c.id] = c.name;
  }

  recalculate();

  // If live prices are already loaded, persist the cache now. Otherwise the
  // save happens in onPricesLoaded() once both datasets are available.
  if (api->arePricesLoaded() && !inventoryItems.isEmpty())
    saveCache();

  // Request contents for every storage unit we know about
  for (const auto &c : containers)
    companion->requestStorageUnit(c.id);
}

void DashboardWidget::onStorageUnitReceived(const QString &casketId,
                                            const QList<GCItem> &items) {
  storageItems[casketId] = items;
  recalculate();
}

void DashboardWidget::onPricesLoaded() {
  recalculate();

  // Once we have live prices AND inventory data, persist everything to the
  // cache so the next launch can display values immediately.
  if (!inventoryItems.isEmpty())
    saveCache();

  statusLabel->setText(
      QString("Prices loaded \u00b7 %1 items \u00b7 Last updated %2")
          .arg(api->priceCount())
          .arg(api->lastUpdated()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Calculation
// ─────────────────────────────────────────────────────────────────────────────

void DashboardWidget::recalculate() {
  // Use live prices when available, fall back to cached prices from the last
  // successful session so values appear instantly on launch.
  auto price = [this](const QString &hashName) -> double {
    double p = api->fetchPrice(hashName);
    return p > 0.0 ? p : cachedPrices.value(hashName, 0.0);
  };

  // Inventory
  inventoryValue = 0.0;
  inventoryCount = inventoryItems.size();
  for (const auto &item : inventoryItems)
    inventoryValue += price(item.marketHashName);

  // Storage units
  storageValue = 0.0;
  storageCount = 0;
  for (auto it = storageItems.constBegin(); it != storageItems.constEnd();
       ++it) {
    storageCount += it.value().size();
    for (const auto &item : it.value())
      storageValue += price(item.marketHashName);
  }

  double total = inventoryValue + storageValue;
  int totalItems = inventoryCount + storageCount;

  // Update card labels
  auto fmt = [](double v) -> QString {
    return QString("$%L1").arg(v, 0, 'f', 2);
  };

  totalValueLabel->setText(fmt(total));
  totalCountLabel->setText(QString("%L1 items").arg(totalItems));

  storageValueLabel->setText(fmt(storageValue));
  storageCountLabel->setText(QString("%L1 items").arg(storageCount));

  inventoryValueLabel->setText(fmt(inventoryValue));
  inventoryCountLabel->setText(QString("%L1 items").arg(inventoryCount));

  rebuildTopItems();
}

// ─────────────────────────────────────────────────────────────────────────────
// Cache
// ─────────────────────────────────────────────────────────────────────────────

QString DashboardWidget::cacheFilePath() const {
  QString accountId = accountManager->activeAccountId();
  if (accountId.isEmpty())
    return {};
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         "/inventory-cache-" + accountId + ".json";
}

static GCItem gcItemFromJson(const QJsonObject &obj) {
  GCItem item;
  item.name = obj["name"].toString();
  item.marketHashName = obj["market_hash_name"].toString();
  item.exterior = obj["exterior"].toString();
  item.iconUrl = obj["icon_url"].toString();
  item.paintWear = obj["paint_wear"].toDouble();
  return item;
}

static QJsonObject gcItemToJson(const GCItem &item, double cachedPrice) {
  QJsonObject obj;
  obj["name"] = item.name;
  obj["market_hash_name"] = item.marketHashName;
  obj["exterior"] = item.exterior;
  obj["icon_url"] = item.iconUrl;
  obj["paint_wear"] = item.paintWear;
  obj["cached_price"] = cachedPrice;
  return obj;
}

void DashboardWidget::loadCache() {
  QString path = cacheFilePath();
  if (path.isEmpty())
    return;

  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return;

  QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  f.close();

  if (!doc.isObject())
    return;

  QJsonObject root = doc.object();

  // Rebuild inventory items
  QList<GCItem> items;
  for (const QJsonValue &v : root["inventory"].toArray()) {
    QJsonObject obj = v.toObject();
    GCItem item = gcItemFromJson(obj);
    double p = obj["cached_price"].toDouble();
    if (!item.marketHashName.isEmpty()) {
      items.append(item);
      if (p > 0.0)
        cachedPrices[item.marketHashName] = p;
    }
  }
  if (!items.isEmpty())
    inventoryItems = items;

  // Rebuild storage unit items
  QJsonObject suObj = root["storage_units"].toObject();
  for (auto it = suObj.constBegin(); it != suObj.constEnd(); ++it) {
    QString casketId = it.key();
    QList<GCItem> suItems;
    for (const QJsonValue &v : it.value().toArray()) {
      QJsonObject obj = v.toObject();
      GCItem item = gcItemFromJson(obj);
      double p = obj["cached_price"].toDouble();
      if (!item.marketHashName.isEmpty()) {
        suItems.append(item);
        if (p > 0.0)
          cachedPrices[item.marketHashName] = p;
      }
    }
    if (!suItems.isEmpty())
      storageItems[casketId] = suItems;
  }

  // Rebuild container names
  QJsonObject names = root["container_names"].toObject();
  for (auto it = names.constBegin(); it != names.constEnd(); ++it)
    containerNames[it.key()] = it.value().toString();

  // Show cached values right away (prices from cachedPrices map)
  if (!inventoryItems.isEmpty()) {
    recalculate();
    statusLabel->setText("Showing cached inventory — refreshing...");
  }
}

void DashboardWidget::saveCache() {
  QString path = cacheFilePath();
  if (path.isEmpty())
    return;

  // Build inventory array using live prices (falling back to cached)
  auto livePrice = [this](const QString &hashName) -> double {
    double p = api->fetchPrice(hashName);
    return p > 0.0 ? p : cachedPrices.value(hashName, 0.0);
  };

  QJsonArray invArray;
  for (const auto &item : inventoryItems)
    invArray.append(gcItemToJson(item, livePrice(item.marketHashName)));

  QJsonObject suObj;
  for (auto it = storageItems.constBegin(); it != storageItems.constEnd();
       ++it) {
    QJsonArray suArray;
    for (const auto &item : it.value())
      suArray.append(gcItemToJson(item, livePrice(item.marketHashName)));
    suObj[it.key()] = suArray;
  }

  QJsonObject namesObj;
  for (auto it = containerNames.constBegin(); it != containerNames.constEnd();
       ++it)
    namesObj[it.key()] = it.value();

  QJsonObject root;
  root["saved_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  root["account_id"] = accountManager->activeAccountId();
  root["inventory"] = invArray;
  root["storage_units"] = suObj;
  root["container_names"] = namesObj;

  QFile f(path);
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.close();
  }
}

void DashboardWidget::rebuildTopItems() {
  struct Row {
    QString name;
    QString source;
    double price;
  };

  QList<Row> rows;

  auto price = [this](const QString &hashName) -> double {
    double p = api->fetchPrice(hashName);
    return p > 0.0 ? p : cachedPrices.value(hashName, 0.0);
  };

  for (const auto &item : inventoryItems) {
    double p = price(item.marketHashName);
    if (p > 0.0)
      rows.append({item.name, "Inventory", p});
  }

  for (auto it = storageItems.constBegin(); it != storageItems.constEnd();
       ++it) {
    QString srcName = containerNames.value(it.key(), "Storage Unit");
    for (const auto &item : it.value()) {
      double p = price(item.marketHashName);
      if (p > 0.0)
        rows.append({item.name, srcName, p});
    }
  }

  std::sort(rows.begin(), rows.end(),
            [](const Row &a, const Row &b) { return a.price > b.price; });

  int count = qMin(rows.size(), 15);
  topItemsTable->setRowCount(count);

  for (int i = 0; i < count; ++i) {
    const auto &r = rows[i];

    auto *nameItem = new QTableWidgetItem(r.name);
    auto *sourceItem = new QTableWidgetItem(r.source);
    auto *priceItem =
        new QTableWidgetItem(QString("$%L1").arg(r.price, 0, 'f', 2));

    priceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    priceItem->setFont(QFont("JetBrains Mono", -1));

    topItemsTable->setItem(i, 0, nameItem);
    topItemsTable->setItem(i, 1, sourceItem);
    topItemsTable->setItem(i, 2, priceItem);
  }
}

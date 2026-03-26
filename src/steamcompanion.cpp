#include "steamcompanion.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>

// Path to the steamcompanion directory relative to the app binary.
// At runtime the binary is in build/bin/ so we go up two levels to reach
// the project root, then into steamcompanion/.
static QString companionDir() {
  // Try next to the binary first (installed layout)
  QString appDir = QCoreApplication::applicationDirPath();
  QString path = appDir + "/steamcompanion";
  if (QDir(path).exists())
    return path;

  // Development layout: binary is in build/bin/, source is two levels up
  path = appDir + "/../../steamcompanion";
  if (QDir(path).exists())
    return QDir(path).absolutePath();

  return path; // best guess
}

SteamCompanion::SteamCompanion(QObject *parent)
    : QObject(parent), process(new QProcess(this)) {
  connect(process, &QProcess::readyReadStandardOutput, this,
          &SteamCompanion::onReadyRead);
  connect(process, &QProcess::errorOccurred, this,
          &SteamCompanion::onProcessError);
  connect(process,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          &SteamCompanion::onProcessFinished);

  // Capture companion stderr for debugging
  connect(process, &QProcess::readyReadStandardError, this, [this]() {
    QString err = QString::fromUtf8(process->readAllStandardError());
    qDebug() << "Companion stderr:" << err.trimmed();
  });
}

SteamCompanion::~SteamCompanion() { stop(); }

void SteamCompanion::start(const QString &profilePath) {
  if (process->state() != QProcess::NotRunning)
    return;

  gcReady_ = false;

#ifdef Q_OS_WIN
  QString standaloneExe =
      QCoreApplication::applicationDirPath() + "/steamcompanion.exe";
  if (QFile::exists(standaloneExe)) {
    process->setWorkingDirectory(QCoreApplication::applicationDirPath());
    QStringList args;
    if (!profilePath.isEmpty())
      args << "--profile" << profilePath;
    process->start(standaloneExe, args);
  } else {
    emit errorOccurred("steamcompanion.exe not found at: " + standaloneExe);
    return;
  }
#else
  process->setWorkingDirectory(companionDir());
  QStringList args;
  args << "index.js";
  if (!profilePath.isEmpty())
    args << "--profile" << profilePath;
  process->start("/usr/bin/node", args);
#endif

  if (!process->waitForStarted(5000)) {
    emit errorOccurred("Failed to start companion process.");
  }
}

void SteamCompanion::stop() {
  if (process->state() == QProcess::NotRunning)
    return;

  sendCommand(QJsonObject{{"command", "quit"}});
  if (!process->waitForFinished(3000)) {
    process->kill();
  }
  gcReady_ = false;
}

bool SteamCompanion::isRunning() const {
  return process->state() == QProcess::Running;
}

bool SteamCompanion::isGCReady() const { return gcReady_; }

void SteamCompanion::sendCommand(const QJsonObject &cmd) {
  if (process->state() != QProcess::Running)
    return;
  QByteArray line = QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n";
  process->write(line);
}

void SteamCompanion::requestInventory() {
  sendCommand(QJsonObject{{"command", "get_inventory"}});
}

void SteamCompanion::requestStorageUnit(const QString &casketId) {
  sendCommand(
      QJsonObject{{"command", "get_storage_unit"}, {"casket_id", casketId}});
}

// ── stdout handler
// ────────────────────────────────────────────────────────────

void SteamCompanion::onReadyRead() {
  // The companion writes one JSON object per line.
  // We read all available lines and handle each one.
  while (process->canReadLine()) {
    QByteArray line = process->readLine().trimmed();
    if (line.isEmpty())
      continue;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
      qWarning() << "SteamCompanion: bad JSON:" << line;
      continue;
    }

    handleMessage(doc.object());
  }
}

void SteamCompanion::handleMessage(const QJsonObject &msg) {
  QString type = msg["type"].toString();

  if (type == "status") {
    QString state = msg["state"].toString();

    if (state == "qr_code") {
      emit qrCodeReady(msg["url"].toString());

    } else if (state == "qr_scanned") {
      emit qrScanned();
      emit statusMessage("QR code scanned — approve login in the Steam app.");

    } else if (state == "logged_in") {
      emit loggedIn(msg["steamid"].toString());
      emit statusMessage("Logged in to Steam.");

    } else if (state == "gc_ready") {
      gcReady_ = true;
      emit gcReady();
      emit statusMessage("Connected to CS2 Game Coordinator.");

    } else if (state == "gc_inventory_loaded") {
      QJsonArray arr = msg["caskets"].toArray();
      QList<GCContainer> containers;
      for (const QJsonValue &v : arr) {
        QJsonObject o = v.toObject();
        GCContainer c;
        c.id = o["id"].toString();
        c.name = o["name"].toString();
        containers.append(c);
      }
      // Emit the containers so the UI can populate the storage unit dropdown
      if (!containers.isEmpty()) {
        emit inventoryReceived(QList<GCItem>(), containers);
      }
      emit statusMessage(QString("GC ready. Found %1 storage unit(s).")
                             .arg(containers.size()));

    } else if (state == "gc_connecting") {
      emit statusMessage("Connecting to CS2 Game Coordinator...");

    } else if (state == "disconnected") {
      gcReady_ = false;
      emit disconnected(msg["reason"].toString());

    } else if (state == "using_saved_token") {
      emit statusMessage("Using saved login token...");

    } else if (state == "authenticated") {
      emit statusMessage("Authenticated with Steam.");

    } else {
      // Forward any other status as a generic message
      QString detail = msg["message"].toString();
      if (!detail.isEmpty())
        emit statusMessage(detail);
    }

  } else if (type == "inventory") {
    QList<GCItem> items;
    QList<GCContainer> containers;

    for (const QJsonValue &v : msg["items"].toArray())
      items.append(parseItem(v.toObject()));

    for (const QJsonValue &v : msg["containers"].toArray()) {
      QJsonObject o = v.toObject();
      GCContainer c;
      c.id = o["id"].toString();
      c.name = o["name"].toString();
      containers.append(c);
    }

    emit inventoryReceived(items, containers);

  } else if (type == "storage_unit") {
    QString casketId = msg["casket_id"].toString();
    QList<GCItem> items;
    for (const QJsonValue &v : msg["items"].toArray())
      items.append(parseItem(v.toObject()));
    emit storageUnitReceived(casketId, items);

  } else if (type == "transfer_complete") {
    emit transferComplete(msg["action"].toString(), msg["casket_id"].toString(),
                          msg["item_id"].toString());

  } else if (type == "floats") {
    QMap<QString, GCItem> updates;
    for (const QJsonValue &v : msg["items"].toArray()) {
      QJsonObject o = v.toObject();
      QString id = o["id"].toString();
      if (id.isEmpty())
        continue;
      GCItem item;
      item.id = id;
      item.paintWear = o["float_value"].toDouble();
      item.paintSeed = o["paint_seed"].toInt();
      item.paintIndex = o["paint_index"].toInt();
      updates[id] = item;
    }
    if (!updates.isEmpty())
      emit floatsReceived(updates);

  } else if (type == "trade_offers") {
    QList<TradeOfferData> offers;
    for (const QJsonValue &v : msg["offers"].toArray())
      offers.append(parseTradeOffer(v.toObject()));
    emit tradeOffersReceived(offers);

  } else if (type == "new_trade_offer") {
    emit newTradeOffer(parseTradeOffer(msg["offer"].toObject()));

  } else if (type == "trade_offer_accepted") {
    QString offerId = msg["offer_id"].toString();
    QString status = msg["status"].toString();
    if (status == "family_view") {
      emit familyViewRequired(offerId);
    } else {
      emit tradeOfferAccepted(offerId, status, msg["error"].toString());
    }

  } else if (type == "trade_offer_cancelled") {
    emit tradeOfferCancelled(msg["offer_id"].toString(),
                             msg["status"].toString(),
                             msg["error"].toString());

  } else if (type == "trade_offer_sent") {
    emit tradeOfferSent(msg["offer_id"].toString(),
                        msg["status"].toString(),
                        msg["error"].toString());

  } else if (type == "trade_offer_changed") {
    emit tradeOfferChanged(msg["offer_id"].toString(),
                           msg["new_state"].toInt());

  } else if (type == "parental_unlock") {
    emit parentalUnlockResult(msg["success"].toBool(),
                              msg["error"].toString());

  } else if (type == "error") {
    emit errorOccurred(msg["message"].toString());
  }
}

GCItem SteamCompanion::parseItem(const QJsonObject &obj) const {
  GCItem item;
  item.id = obj["id"].toString();
  item.name = obj["name"].toString();
  item.marketHashName = obj["market_hash_name"].toString();
  item.exterior = obj["exterior"].toString();
  item.paintWear = obj["paint_wear"].toDouble();
  item.paintIndex = obj["paint_index"].toInt();
  item.paintSeed = obj["paint_seed"].toInt();
  item.defIndex = obj["def_index"].toInt();
  item.quality = obj["quality"].toInt();
  item.rarity = obj["rarity"].toInt();
  item.customName = obj["custom_name"].toString();
  item.casketId = obj["casket_id"].toString();
  item.tradable = obj["tradable"].toBool();
  item.marketable = obj["marketable"].toBool();
  item.iconUrl = obj["icon_url"].toString();
  item.inspectLink = obj["inspect_link"].toString();
  return item;
}

// ── Process error handlers
// ────────────────────────────────────────────────────

void SteamCompanion::onProcessError(QProcess::ProcessError error) {
  QString msg;
  switch (error) {
  case QProcess::FailedToStart:
    msg = "Failed to start companion process. Check that node is at "
          "/usr/bin/node.";
    break;
  case QProcess::Crashed:
    msg = "Companion process crashed.";
    break;
  case QProcess::Timedout:
    msg = "Companion process timed out.";
    break;
  default:
    msg = "Companion process error.";
    break;
  }
  gcReady_ = false;
  emit errorOccurred(msg);
}

void SteamCompanion::onProcessFinished(int exitCode,
                                       QProcess::ExitStatus status) {
  gcReady_ = false;
  if (status == QProcess::CrashExit) {
    emit errorOccurred("Companion process exited unexpectedly.");
  } else if (exitCode != 0) {
    emit statusMessage(
        QString("Companion process exited with code %1.").arg(exitCode));
  }
}

void SteamCompanion::addToStorageUnit(const QString &casketId,
                                      const QString &itemId) {
  sendCommand(QJsonObject{{"command", "add_to_storage_unit"},
                          {"casket_id", casketId},
                          {"item_id", itemId}});
}

void SteamCompanion::removeFromStorageUnit(const QString &casketId,
                                           const QString &itemId) {
  sendCommand(QJsonObject{{"command", "remove_from_storage_unit"},
                          {"casket_id", casketId},
                          {"item_id", itemId}});
}

void SteamCompanion::requestTradeOffers() {
  sendCommand(QJsonObject{{"command", "get_trade_offers"}});
}

void SteamCompanion::acceptTradeOffer(const QString &offerId) {
  sendCommand(
      QJsonObject{{"command", "accept_trade_offer"}, {"offer_id", offerId}});
}

void SteamCompanion::cancelTradeOffer(const QString &offerId) {
  sendCommand(
      QJsonObject{{"command", "cancel_trade_offer"}, {"offer_id", offerId}});
}

void SteamCompanion::sendTradeOffer(const QString &tradeUrl,
                                    const QStringList &itemsToGive,
                                    const QStringList &itemsToReceive,
                                    const QString &message) {
  QJsonArray give, recv;
  for (const QString &id : itemsToGive)
    give.append(id);
  for (const QString &id : itemsToReceive)
    recv.append(id);
  sendCommand(QJsonObject{{"command", "send_trade_offer"},
                           {"trade_url", tradeUrl},
                           {"items_to_give", give},
                           {"items_to_receive", recv},
                           {"message", message}});
}

void SteamCompanion::unlockParentalView(const QString &pin) {
  sendCommand(QJsonObject{{"command", "parental_unlock"}, {"pin", pin}});
}

void SteamCompanion::requestFloats(const QList<GCItem> &items) {
  QJsonArray arr;
  for (const GCItem &item : items) {
    if (item.inspectLink.isEmpty() || item.paintWear > 0.0)
      continue;
    QJsonObject o;
    o["id"] = item.id;
    o["inspect_link"] = item.inspectLink;
    arr.append(o);
  }
  if (arr.isEmpty())
    return;
  sendCommand(QJsonObject{{"command", "get_floats"}, {"items", arr}});
}

TradeOfferData
SteamCompanion::parseTradeOffer(const QJsonObject &obj) const {
  TradeOfferData offer;
  offer.id = obj["id"].toString();
  offer.partnerSteamId = obj["partner"].toString();
  offer.message = obj["message"].toString();
  offer.isOurOffer = obj["is_our_offer"].toBool();
  offer.timeCreated = obj["time_created"].toVariant().toLongLong();
  offer.state = obj["state"].toInt();

  for (const QJsonValue &v : obj["items_to_give"].toArray()) {
    QJsonObject o = v.toObject();
    TradeItem item;
    item.assetId = o["assetid"].toString();
    item.marketHashName = o["market_hash_name"].toString();
    item.appId = o["appid"].toInt(730);
    offer.itemsToGive.append(item);
  }
  for (const QJsonValue &v : obj["items_to_receive"].toArray()) {
    QJsonObject o = v.toObject();
    TradeItem item;
    item.assetId = o["assetid"].toString();
    item.marketHashName = o["market_hash_name"].toString();
    item.appId = o["appid"].toInt(730);
    offer.itemsToReceive.append(item);
  }
  return offer;
}

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
}

SteamCompanion::~SteamCompanion() { stop(); }

void SteamCompanion::start() {
  if (process->state() != QProcess::NotRunning)
    return;

  gcReady_ = false;

  process->setWorkingDirectory(companionDir());

#ifdef Q_OS_WIN
  process->start("node.exe", QStringList() << "index.js");
#else
  process->start("/usr/bin/node", QStringList() << "index.js");
#endif

  if (!process->waitForStarted(5000)) {
    emit errorOccurred("Failed to start Node.js companion process.");
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
    emit transferComplete(
        msg["action"].toString(),
        msg["casket_id"].toString(),
        msg["item_id"].toString()
    );

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

void SteamCompanion::addToStorageUnit(const QString &casketId, const QString &itemId)
{
    sendCommand(QJsonObject{
        {"command", "add_to_storage_unit"},
        {"casket_id", casketId},
        {"item_id", itemId}
    });
}

void SteamCompanion::removeFromStorageUnit(const QString &casketId, const QString &itemId)
{
    sendCommand(QJsonObject{
        {"command", "remove_from_storage_unit"},
        {"casket_id", casketId},
        {"item_id", itemId}
    });
}

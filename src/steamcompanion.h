#ifndef STEAMCOMPANION_H
#define STEAMCOMPANION_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>

struct GCItem {
  QString id;
  QString name;
  QString marketHashName;
  QString exterior;
  double paintWear = 0.0;
  int paintIndex = 0;
  int paintSeed = 0;
  int defIndex = 0;
  int quality = 0;
  int rarity = 0;
  QString customName;
  QString casketId;
  bool tradable = false;
  bool marketable = false;
  QString iconUrl;
};

struct GCContainer {
  QString id;
  QString name;
};

class SteamCompanion : public QObject {
  Q_OBJECT

public:
  explicit SteamCompanion(QObject *parent = nullptr);
  ~SteamCompanion();

  void stop();
  bool isRunning() const;
  bool isGCReady() const;
  void start(const QString &profilePath = QString());

  void sendCommand(const QJsonObject &cmd);
  void requestInventory();
  void requestStorageUnit(const QString &casketId);
  void addToStorageUnit(const QString &casketId, const QString &itemId);
  void removeFromStorageUnit(const QString &casketId, const QString &itemId);

signals:
  // Login flow
  void qrCodeReady(const QString &url);
  void qrScanned();
  void loggedIn(const QString &steamId);
  void gcReady();
  void disconnected(const QString &reason);
  void transferComplete(const QString &action, const QString &casketId,
                        const QString &itemId);

  // Data
  void inventoryReceived(const QList<GCItem> &items,
                         const QList<GCContainer> &containers);
  void storageUnitReceived(const QString &casketId, const QList<GCItem> &items);

  // Status / errors
  void statusMessage(const QString &message);
  void errorOccurred(const QString &message);

private slots:
  void onReadyRead();
  void onProcessError(QProcess::ProcessError error);
  void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
  void handleMessage(const QJsonObject &msg);
  GCItem parseItem(const QJsonObject &obj) const;

  QProcess *process;
  bool gcReady_ = false;
};

#endif // STEAMCOMPANION_H

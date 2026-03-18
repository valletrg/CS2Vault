#ifndef STEAMAPI_H
#define STEAMAPI_H

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

struct SteamInventoryItem {
  QString assetId;
  QString classId;
  QString instanceId;
  QString marketHashName;
  QString iconUrl;
  QString type;
  QString name;
  QString rarity;
  QString exterior;
  double float_value;
  bool tradable;
  bool marketable;
};

struct SteamProfile {
  QString steamId;
  QString personaName;
  QString avatarUrl;
  QString profileUrl;
  bool isLoggedIn;
};

class SteamAPI : public QObject {
  Q_OBJECT

public:
  explicit SteamAPI(QObject *parent = nullptr);
  ~SteamAPI();

  void loginWithSteamId(const QString &steamId);
  SteamProfile getProfile() const;
  bool isLoggedIn() const;
  void logout();
  void fetchInventory(const QString &steamId, int appId = 730,
                      int contextId = 2);
  QVector<SteamInventoryItem> getInventory() const;

signals:
  void loginSuccessful(const SteamProfile &profile);
  void loginFailed(const QString &error);
  void inventoryFetched(const QVector<SteamInventoryItem> &items);
  void inventoryError(const QString &error);

private:
  void fetchProfile(const QString &steamId);
  QVector<SteamInventoryItem> parseInventory(const QByteArray &data);

  QNetworkAccessManager *networkManager;
  SteamProfile currentProfile;
  QVector<SteamInventoryItem> inventory;
};

#endif

#include "steamapi.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <QRegularExpression>

SteamAPI::SteamAPI(QObject *parent)
    : QObject(parent)
    , networkManager(new QNetworkAccessManager(this))
{
    currentProfile.isLoggedIn = false;
}

SteamAPI::~SteamAPI() = default;

void SteamAPI::loginWithSteamId(const QString &steamId)
{
    if (steamId.isEmpty()) {
        emit loginFailed("Steam ID cannot be empty");
        return;
    }

    fetchProfile(steamId);
}

void SteamAPI::fetchProfile(const QString &steamId)
{
    QString urlStr = QString("https://steamcommunity.com/profiles/%1/?xml=1").arg(steamId);
    
    QNetworkRequest request{QUrl(urlStr)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0");
    
    QNetworkReply *reply = networkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, steamId]() {
        QByteArray data = reply->readAll();
        reply->deleteLater();
        
        currentProfile.steamId = steamId;
        currentProfile.personaName = "Steam User";
        currentProfile.profileUrl = QString("https://steamcommunity.com/profiles/%1").arg(steamId);
        currentProfile.isLoggedIn = true;
        
        QString content = QString::fromUtf8(data);
        
        QRegularExpression nameRx(R"(<steamID><!\[CDATA\[(.+?)\]\]>)");
        QRegularExpressionMatch nameMatch = nameRx.match(content);
        if (nameMatch.hasMatch()) {
            currentProfile.personaName = nameMatch.captured(1);
        }
        
        QRegularExpression avatarRx(R"(<avatarMedium><!\[CDATA\[(.+?)\]\]>)");
        QRegularExpressionMatch avatarMatch = avatarRx.match(content);
        if (avatarMatch.hasMatch()) {
            currentProfile.avatarUrl = avatarMatch.captured(1);
        }
        
        emit loginSuccessful(currentProfile);
    });
}

SteamProfile SteamAPI::getProfile() const
{
    return currentProfile;
}

bool SteamAPI::isLoggedIn() const
{
    return currentProfile.isLoggedIn;
}

void SteamAPI::logout()
{
    currentProfile = SteamProfile();
    currentProfile.isLoggedIn = false;
    inventory.clear();
}

void SteamAPI::fetchInventory(const QString &steamId, int appId, int contextId)
{
    QString urlStr = QString("https://steamcommunity.com/inventory/%1/%2/%3?l=english&count=2000")
                         .arg(steamId)
                         .arg(appId)
                         .arg(contextId);
    
    QNetworkRequest request{QUrl(urlStr)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0");
    request.setRawHeader("Accept", "application/json");
    
    QNetworkReply *reply = networkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QByteArray data = reply->readAll();
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        
        if (httpStatus == 400) {
            emit inventoryError("Bad Request - Make sure inventory is public");
            return;
        }
        if (httpStatus == 403) {
            emit inventoryError("Private inventory - Set inventory to public in Steam");
            return;
        }
        if (data.isEmpty()) {
            emit inventoryError("Empty response from Steam");
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) {
            emit inventoryError("Invalid response from Steam");
            return;
        }
        
        QJsonObject obj = doc.object();
        if (obj.contains("error")) {
            emit inventoryError(obj["error"].toString());
            return;
        }
        if (!obj.contains("assets") || !obj.contains("descriptions")) {
            emit inventoryError("No inventory data found");
            return;
        }
        
        inventory = parseInventory(data);
        emit inventoryFetched(inventory);
    });
}

QVector<SteamInventoryItem> SteamAPI::parseInventory(const QByteArray &data)
{
    QVector<SteamInventoryItem> items;
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();
    
    QJsonArray assets = obj["assets"].toArray();
    QJsonArray descriptions = obj["descriptions"].toArray();
    
    // Create a map of descriptions by classid_instanceid
    QMap<QString, QJsonObject> descMap;
    for (const QJsonValue &descVal : descriptions) {
        QJsonObject desc = descVal.toObject();
        QString key = QString("%1_%2").arg(desc["classid"].toString(), desc["instanceid"].toString());
        descMap[key] = desc;
    }
    
    // Parse assets and match with descriptions
    for (const QJsonValue &assetVal : assets) {
        QJsonObject asset = assetVal.toObject();
        
        SteamInventoryItem item;
        item.assetId = asset["assetid"].toString();
        item.classId = asset["classid"].toString();
        item.instanceId = asset["instanceid"].toString();
        
        QString key = QString("%1_%2").arg(item.classId, item.instanceId);
        
        if (descMap.contains(key)) {
            QJsonObject desc = descMap[key];
            
            item.marketHashName = desc["market_hash_name"].toString();
            item.name = desc["name"].toString();
            item.type = desc["type"].toString();
            item.iconUrl = desc["icon_url"].toString();
            item.tradable = desc["tradable"].toBool();
            item.marketable = desc["marketable"].toBool();
            
            // Parse tags for rarity and exterior
            QJsonArray tags = desc["tags"].toArray();
            for (const QJsonValue &tagVal : tags) {
                QJsonObject tag = tagVal.toObject();
                QString category = tag["category"].toString();
                
                if (category == "Rarity") {
                    item.rarity = tag["name"].toString();
                } else if (category == "Exterior") {
                    item.exterior = tag["name"].toString();
                }
            }
            
            if (!item.marketHashName.isEmpty()) {
                items.append(item);
            }
        }
    }
    
    return items;
}

QVector<SteamInventoryItem> SteamAPI::getInventory() const
{
    return inventory;
}

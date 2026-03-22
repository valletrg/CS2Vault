#include "accountmanager.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

AccountManager::AccountManager(QObject *parent) : QObject(parent) {}

QString AccountManager::accountsDir() const {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         "/accounts";
}

QString AccountManager::tokenPath(const QString &id) const {
  return accountsDir() + "/" + id + "/refresh_token.txt";
}

void AccountManager::load() {
  m_accounts.clear();

  QDir dir(accountsDir());
  if (!dir.exists()) {
    dir.mkpath(".");
    return;
  }

  // Each subdirectory is an account
  const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString &entry : entries) {
    QString metaPath = accountsDir() + "/" + entry + "/meta.json";
    QFile f(metaPath);
    if (!f.open(QIODevice::ReadOnly))
      continue;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    if (!doc.isObject())
      continue;
    SteamAccount acc = accountFromJson(doc.object());
    acc.tokenPath = tokenPath(acc.id);
    m_accounts.append(acc);
  }

  // Restore last active account
  QString settingsPath = accountsDir() + "/active.txt";
  QFile af(settingsPath);
  if (af.open(QIODevice::ReadOnly)) {
    m_activeId = QString::fromUtf8(af.readAll()).trimmed();
    af.close();
  }

  // If saved active id no longer exists, default to first account
  if (!m_activeId.isEmpty() && !hasAccount(m_activeId)) {
    m_activeId.clear();
  }
  if (m_activeId.isEmpty() && !m_accounts.isEmpty()) {
    m_activeId = m_accounts.first().id;
  }

  qInfo() << "AccountManager: loaded" << m_accounts.size()
          << "accounts, active:" << m_activeId;
}

QString AccountManager::addAccount(const QString &steamId,
                                   const QString &personaName,
                                   const QString &avatarUrl,
                                   const QString &refreshToken) {
  // Check if this Steam ID is already saved
  for (const SteamAccount &acc : m_accounts) {
    if (acc.steamId == steamId) {
      qInfo() << "Account already exists for steamId" << steamId;
      return acc.id;
    }
  }

  SteamAccount acc;
  acc.id = QUuid::createUuid().toString(QUuid::Id128).left(8); // short id
  acc.steamId = steamId;
  acc.personaName = personaName;
  acc.avatarUrl = avatarUrl;
  acc.addedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
  acc.tokenPath = tokenPath(acc.id);

  // Create account directory
  QDir().mkpath(accountsDir() + "/" + acc.id);

  // Save the refresh token
  QFile tf(acc.tokenPath);
  if (tf.open(QIODevice::WriteOnly)) {
    tf.write(refreshToken.toUtf8());
    tf.close();
  }

  saveAccount(acc);
  m_accounts.append(acc);

  // If this is the first account, make it active
  if (m_accounts.size() == 1) {
    setActiveAccount(acc.id);
  }

  emit accountAdded(acc);
  emit accountsChanged();
  return acc.id;
}

QString AccountManager::registerAccount(const QString &steamId,
                                        const QString &personaName,
                                        const QString &avatarUrl,
                                        const QString &existingId) {
  // If this Steam ID is already tracked, just update the metadata.
  for (SteamAccount &acc : m_accounts) {
    if (acc.steamId == steamId) {
      if (!personaName.isEmpty() || !avatarUrl.isEmpty())
        updateAccountMeta(acc.id, personaName, avatarUrl);
      return acc.id;
    }
  }

  SteamAccount acc;
  acc.id = existingId.isEmpty()
               ? QUuid::createUuid().toString(QUuid::Id128).left(8)
               : existingId;
  acc.steamId = steamId;
  acc.personaName = personaName;
  acc.avatarUrl = avatarUrl;
  acc.addedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
  acc.tokenPath = tokenPath(acc.id);

  // Ensure the directory exists (the companion may have already created it).
  QDir().mkpath(accountsDir() + "/" + acc.id);

  // Do NOT write the token file — the companion already saved it here.
  saveAccount(acc);
  m_accounts.append(acc);

  if (m_accounts.size() == 1)
    setActiveAccount(acc.id);

  emit accountAdded(acc);
  emit accountsChanged();
  return acc.id;
}

bool AccountManager::removeAccount(const QString &id) {
  int idx = -1;
  for (int i = 0; i < m_accounts.size(); ++i) {
    if (m_accounts[i].id == id) {
      idx = i;
      break;
    }
  }
  if (idx < 0)
    return false;

  // Delete account folder
  QDir dir(accountsDir() + "/" + id);
  dir.removeRecursively();

  m_accounts.removeAt(idx);

  // If we removed the active account, switch to first available
  if (m_activeId == id) {
    m_activeId = m_accounts.isEmpty() ? QString() : m_accounts.first().id;
    // Save new active
    if (!m_activeId.isEmpty()) {
      QFile af(accountsDir() + "/active.txt");
      if (af.open(QIODevice::WriteOnly))
        af.write(m_activeId.toUtf8());
    }
    emit activeAccountChanged(m_activeId);
  }

  emit accountRemoved(id);
  emit accountsChanged();
  return true;
}

bool AccountManager::setActiveAccount(const QString &id) {
  if (!hasAccount(id))
    return false;
  m_activeId = id;

  QFile af(accountsDir() + "/active.txt");
  if (af.open(QIODevice::WriteOnly)) {
    af.write(id.toUtf8());
    af.close();
  }

  emit activeAccountChanged(id);
  return true;
}

void AccountManager::updateAccountMeta(const QString &id,
                                       const QString &personaName,
                                       const QString &avatarUrl) {
  for (SteamAccount &acc : m_accounts) {
    if (acc.id == id) {
      acc.personaName = personaName;
      acc.avatarUrl = avatarUrl;
      saveAccount(acc);
      emit accountsChanged();
      return;
    }
  }
}

SteamAccount AccountManager::account(const QString &id) const {
  for (const SteamAccount &acc : m_accounts)
    if (acc.id == id)
      return acc;
  return SteamAccount{};
}

bool AccountManager::hasAccount(const QString &id) const {
  for (const SteamAccount &acc : m_accounts)
    if (acc.id == id)
      return true;
  return false;
}

SteamAccount AccountManager::activeAccount() const {
  return account(m_activeId);
}

void AccountManager::saveAccount(const SteamAccount &acc) {
  QString metaPath = accountsDir() + "/" + acc.id + "/meta.json";
  QFile f(metaPath);
  if (!f.open(QIODevice::WriteOnly))
    return;
  f.write(QJsonDocument(accountToJson(acc)).toJson());
  f.close();
}

SteamAccount AccountManager::accountFromJson(const QJsonObject &obj) const {
  SteamAccount acc;
  acc.id = obj["id"].toString();
  acc.steamId = obj["steamId"].toString();
  acc.personaName = obj["personaName"].toString();
  acc.avatarUrl = obj["avatarUrl"].toString();
  acc.addedAt = obj["addedAt"].toString();
  return acc;
}

QJsonObject AccountManager::accountToJson(const SteamAccount &acc) const {
  QJsonObject obj;
  obj["id"] = acc.id;
  obj["steamId"] = acc.steamId;
  obj["personaName"] = acc.personaName;
  obj["avatarUrl"] = acc.avatarUrl;
  obj["addedAt"] = acc.addedAt;
  return obj;
}
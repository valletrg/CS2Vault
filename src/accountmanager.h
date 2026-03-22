#ifndef ACCOUNTMANAGER_H
#define ACCOUNTMANAGER_H

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

struct SteamAccount {
  QString id;          // generated UUID, used as folder name
  QString steamId;     // Steam 64-bit ID
  QString personaName; // display name
  QString avatarUrl;
  QString addedAt;
  QString tokenPath; // full path to encrypted refresh_token.txt
};

class AccountManager : public QObject {
  Q_OBJECT

public:
  explicit AccountManager(QObject *parent = nullptr);

  // Load all saved accounts from disk
  void load();

  // Add a new account after successful login (writes + encrypts token)
  // Returns the new account's id
  QString addAccount(const QString &steamId, const QString &personaName,
                     const QString &avatarUrl, const QString &refreshToken);

  // Register an account whose token file was already written by the companion
  // (does NOT touch the token file — just saves metadata)
  // Pass the pre-generated id used when starting the companion, or leave empty
  // to generate a new one.
  QString registerAccount(const QString &steamId, const QString &personaName,
                          const QString &avatarUrl,
                          const QString &existingId = QString());

  // Remove an account and delete its data
  bool removeAccount(const QString &id);

  // Get all saved accounts
  QList<SteamAccount> accounts() const { return m_accounts; }

  // Get a specific account
  SteamAccount account(const QString &id) const;
  bool hasAccount(const QString &id) const;

  // Active account management
  QString activeAccountId() const { return m_activeId; }
  SteamAccount activeAccount() const;
  bool setActiveAccount(const QString &id);
  bool hasAnyAccounts() const { return !m_accounts.isEmpty(); }

  // Get the token file path for a given account id
  QString tokenPath(const QString &id) const;

  // Update persona name / avatar (called after GC connects)
  void updateAccountMeta(const QString &id, const QString &personaName,
                         const QString &avatarUrl);

signals:
  void accountAdded(const SteamAccount &account);
  void accountRemoved(const QString &id);
  void activeAccountChanged(const QString &id);
  void accountsChanged();

private:
  void saveAccount(const SteamAccount &account);
  SteamAccount accountFromJson(const QJsonObject &obj) const;
  QJsonObject accountToJson(const SteamAccount &account) const;
  QString accountsDir() const;

  QList<SteamAccount> m_accounts;
  QString m_activeId;
};

#endif // ACCOUNTMANAGER_H
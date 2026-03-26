#ifndef TRADESWIDGET_H
#define TRADESWIDGET_H

#include "itemdatabase.h"
#include "priceempireapi.h"
#include "steamcompanion.h"
#include "tradehistory.h"

#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

struct TradeOfferCard {
  QString offerId;
  TradeOfferData offer;
  QFrame *frame = nullptr;
  QLabel *timestampLabel = nullptr;
  QLabel *statusBadge = nullptr;
  QLabel *plLabel = nullptr;
  QPushButton *acceptButton = nullptr;
  QPushButton *declineButton = nullptr;
  QPushButton *cancelButton = nullptr;
  QLabel *statusMessageLabel = nullptr;
  QPushButton *dismissButton = nullptr;
  bool resolved = false;
  bool pendingConfirmation = false;
};

class TradesWidget : public QWidget {
  Q_OBJECT

public:
  explicit TradesWidget(SteamCompanion *companion, PriceEmpireAPI *api,
                        TradeHistoryManager *tradeHistoryManager,
                        ItemDatabase *itemDb, QWidget *parent = nullptr);

signals:
  void navigateToSettings();

public slots:
  void onTradeOffersReceived(const QList<TradeOfferData> &offers);
  void onNewTradeOffer(const TradeOfferData &offer);
  void onTradeOfferAccepted(const QString &offerId, const QString &status,
                            const QString &errorMessage);
  void onTradeOfferCancelled(const QString &offerId, const QString &status,
                             const QString &errorMessage);
  void onTradeOfferChanged(const QString &offerId, int newState);
  void onFamilyViewRequired(const QString &offerId);
  void onParentalUnlockResult(bool success, const QString &error);
  void resetConsent();

private:
  void setupUI();
  QWidget *buildConsentWidget();
  void onConsentAccepted();
  void onConsentDeclined();
  QFrame *createOfferCard(const TradeOfferData &offer);
  QWidget *createItemColumn(const QString &title,
                            const QList<TradeItem> &items);
  void updateTimestamps();
  void resolveCard(const QString &offerId, const QString &badgeText,
                   const QString &badgeColor);
  void dismissCard(const QString &offerId);
  void logTradeToHistory(const TradeOfferData &offer);
  QString relativeTime(qint64 unixTimestamp) const;
  double itemPrice(const QString &marketHashName) const;

  SteamCompanion *companion;
  PriceEmpireAPI *api;
  TradeHistoryManager *tradeHistoryManager;
  ItemDatabase *itemDb;

  QStackedWidget *consentStack = nullptr;
  QStackedWidget *consentCardStack = nullptr;
  bool consentAccepted = false;

  QVBoxLayout *cardLayout = nullptr;
  QScrollArea *scrollArea = nullptr;
  QList<TradeOfferCard> cards;
  QTimer *timestampTimer = nullptr;

  bool familyViewUnlocked = false;
  QString pendingFamilyViewOfferId;
  QDialog *pinDialog = nullptr;
  QLabel *pinErrorLabel = nullptr;

};

#endif // TRADESWIDGET_H

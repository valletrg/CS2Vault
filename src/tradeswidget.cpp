#include "tradeswidget.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollBar>
#include <QGraphicsDropShadowEffect>
#include <QSettings>
#include <QStackedWidget>

// ETradeOfferState values from Steam
namespace ETradeOfferState {
constexpr int Invalid = 1;
constexpr int Active = 2;
constexpr int Accepted = 3;
constexpr int Countered = 4;
constexpr int Expired = 5;
constexpr int Canceled = 6;
constexpr int Declined = 7;
constexpr int InvalidItems = 8;
constexpr int NeedsConfirmation = 9;
constexpr int CanceledBySecondFactor = 10;
constexpr int InEscrow = 11;
} // namespace ETradeOfferState

TradesWidget::TradesWidget(SteamCompanion *companion, PriceEmpireAPI *api,
                           TradeHistoryManager *tradeHistoryManager,
                           ItemDatabase *itemDb, QWidget *parent)
    : QWidget(parent), companion(companion), api(api),
      tradeHistoryManager(tradeHistoryManager), itemDb(itemDb) {
  setupUI();

  timestampTimer = new QTimer(this);
  connect(timestampTimer, &QTimer::timeout, this,
          &TradesWidget::updateTimestamps);
  timestampTimer->start(60000);

  connect(companion, &SteamCompanion::familyViewRequired, this,
          &TradesWidget::onFamilyViewRequired);
  connect(companion, &SteamCompanion::parentalUnlockResult, this,
          &TradesWidget::onParentalUnlockResult);

  // Only request trade offers once GC is ready AND consent has been given.
  connect(companion, &SteamCompanion::gcReady, this, [this]() {
    if (consentAccepted)
      this->companion->requestTradeOffers();
  });
}

void TradesWidget::setupUI() {
  auto *rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);

  consentStack = new QStackedWidget(this);
  rootLayout->addWidget(consentStack);

  // ── Page 0: consent overlay ──────────────────────────────────────────────
  consentStack->addWidget(buildConsentWidget());

  // ── Page 1: normal trades UI ─────────────────────────────────────────────
  auto *tradesPage = new QWidget(consentStack);
  auto *mainLayout = new QVBoxLayout(tradesPage);
  mainLayout->setContentsMargins(24, 24, 24, 24);

  auto *headerRow = new QHBoxLayout();
  auto *header = new QLabel("Trade Offers", tradesPage);
  header->setStyleSheet("font-size: 20px; font-weight: bold; color: #e2e8f0;");
  headerRow->addWidget(header);
  headerRow->addStretch();

  auto *refreshBtn = new QPushButton("Refresh", tradesPage);
  refreshBtn->setCursor(Qt::PointingHandCursor);
  connect(refreshBtn, &QPushButton::clicked, this,
          [this]() { companion->requestTradeOffers(); });
  headerRow->addWidget(refreshBtn);
  mainLayout->addLayout(headerRow);
  mainLayout->addSpacing(8);

  scrollArea = new QScrollArea(tradesPage);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto *scrollContent = new QWidget(scrollArea);
  cardLayout = new QVBoxLayout(scrollContent);
  cardLayout->setContentsMargins(0, 0, 0, 0);
  cardLayout->setSpacing(12);
  cardLayout->addStretch();

  scrollArea->setWidget(scrollContent);
  mainLayout->addWidget(scrollArea, 1);

  consentStack->addWidget(tradesPage);

  // Show correct page based on persisted consent
  QSettings settings("CS2Vault", "Settings");
  consentAccepted = settings.value("trades/consentAccepted", false).toBool();
  consentStack->setCurrentIndex(consentAccepted ? 1 : 0);
}

QWidget *TradesWidget::buildConsentWidget() {
  // Semi-transparent dark overlay so any trades content behind reads as
  // "locked" without being completely black.
  auto *page = new QWidget(this);
  page->setAttribute(Qt::WA_StyledBackground, true);
  page->setStyleSheet("background-color: rgba(0, 0, 0, 0.70);");

  auto *outerLayout = new QVBoxLayout(page);
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->addStretch();

  auto *hLayout = new QHBoxLayout();
  hLayout->setContentsMargins(0, 0, 0, 0);
  hLayout->addStretch();

  // ── Card shell ────────────────────────────────────────────────────────────
  auto *card = new QFrame(page);
  card->setObjectName("consentCard");
  // 520px total width; 36px padding each side → ~448px readable body width.
  card->setFixedWidth(520);
  card->setStyleSheet(
      "QFrame#consentCard {"
      "  background-color: #1a1d26;"
      "  border-radius: 12px;"
      "  border: 1px solid #2a2d3a;"
      "}");

  // Drop shadow to lift the card off the overlay
  auto *shadow = new QGraphicsDropShadowEffect(card);
  shadow->setBlurRadius(40);
  shadow->setOffset(0, 8);
  shadow->setColor(QColor(0, 0, 0, 180));
  card->setGraphicsEffect(shadow);

  auto *cardOuterLayout = new QVBoxLayout(card);
  cardOuterLayout->setContentsMargins(36, 36, 36, 36);
  cardOuterLayout->setSpacing(0);

  consentCardStack = new QStackedWidget(card);
  consentCardStack->setStyleSheet("background: transparent;");
  cardOuterLayout->addWidget(consentCardStack);

  // ── Card page 0: consent form ─────────────────────────────────────────────
  auto *consentPage = new QWidget(consentCardStack);
  consentPage->setStyleSheet("background: transparent;");
  auto *consentLayout = new QVBoxLayout(consentPage);
  consentLayout->setContentsMargins(0, 0, 0, 0);
  consentLayout->setSpacing(0);

  auto *title = new QLabel("Before you use Trade Offers", consentPage);
  title->setStyleSheet(
      "font-size: 19px; font-weight: bold; color: #e2e8f0;"
      "margin-bottom: 14px;");
  consentLayout->addWidget(title);

  // Each paragraph as a separate label so we can space them individually.
  const char *paragraphs[] = {
      "CS2Vault can display your incoming and outgoing Steam trade offers "
      "and lets you accept or decline them directly from the app.",

      "If you have Steam Family View (parental controls) enabled on your "
      "account, you will be asked to enter your Family View PIN when "
      "accepting a trade. This PIN is sent directly to Steam and is never "
      "stored by CS2Vault.",

      "CS2Vault is open source. You can verify exactly what happens with "
      "your data at any time by reviewing the source code on GitHub.",

      "By continuing you confirm you understand how this feature works."
  };

  for (int i = 0; i < 4; ++i) {
    auto *para = new QLabel(paragraphs[i], consentPage);
    para->setWordWrap(true);
    // Last paragraph gets a slightly different colour to read as a
    // confirmation statement rather than informational text.
    QString color = (i == 3) ? "#cbd5e1" : "#b0b8c8";
    // Add top margin on every paragraph after the first.
    QString margin = (i == 0) ? "" : "margin-top: 12px;";
    para->setStyleSheet(
        QString("font-size: 13px; color: %1; line-height: 1.5; %2")
            .arg(color, margin));
    consentLayout->addWidget(para);
  }

  // Spacer above buttons
  consentLayout->addSpacing(24);

  auto *btnRow = new QHBoxLayout();
  btnRow->setSpacing(10);
  btnRow->addStretch();

  auto *acceptBtn = new QPushButton("I understand, continue", consentPage);
  acceptBtn->setCursor(Qt::PointingHandCursor);
  acceptBtn->setStyleSheet(
      "QPushButton {"
      "  background-color: #4fc3f7; color: #0d1117;"
      "  border: none; border-radius: 7px;"
      "  padding: 9px 22px; font-weight: bold; font-size: 13px;"
      "}"
      "QPushButton:hover { background-color: #81d4fa; }"
      "QPushButton:pressed { background-color: #29b6f6; }");

  auto *declineBtn = new QPushButton("No thanks", consentPage);
  declineBtn->setCursor(Qt::PointingHandCursor);
  declineBtn->setStyleSheet(
      "QPushButton {"
      "  background-color: transparent; color: #64748b;"
      "  border: 1px solid #2a2d3a; border-radius: 7px;"
      "  padding: 9px 22px; font-size: 13px;"
      "}"
      "QPushButton:hover { border-color: #475569; color: #94a3b8; }"
      "QPushButton:pressed { background-color: #1e2433; }");

  btnRow->addWidget(acceptBtn);
  btnRow->addWidget(declineBtn);
  consentLayout->addLayout(btnRow);

  consentCardStack->addWidget(consentPage); // index 0

  // ── Card page 1: declined state ───────────────────────────────────────────
  auto *declinedPage = new QWidget(consentCardStack);
  declinedPage->setStyleSheet("background: transparent;");
  auto *declinedLayout = new QVBoxLayout(declinedPage);
  declinedLayout->setContentsMargins(0, 0, 0, 0);
  declinedLayout->setSpacing(0);

  auto *declinedTitle = new QLabel("Trade Offers Disabled", declinedPage);
  declinedTitle->setStyleSheet(
      "font-size: 19px; font-weight: bold; color: #e2e8f0;"
      "margin-bottom: 14px;");
  declinedLayout->addWidget(declinedTitle);

  auto *declinedBody = new QLabel(
      "You can enable trade offers at any time by going to Settings and "
      "resetting the Trades tab consent.",
      declinedPage);
  declinedBody->setStyleSheet("font-size: 13px; color: #b0b8c8;");
  declinedBody->setWordWrap(true);
  declinedLayout->addWidget(declinedBody);

  declinedLayout->addSpacing(24);

  auto *settingsBtnRow = new QHBoxLayout();
  settingsBtnRow->addStretch();
  auto *goSettingsBtn = new QPushButton("Go to Settings", declinedPage);
  goSettingsBtn->setCursor(Qt::PointingHandCursor);
  goSettingsBtn->setStyleSheet(
      "QPushButton {"
      "  background-color: transparent; color: #64748b;"
      "  border: 1px solid #2a2d3a; border-radius: 7px;"
      "  padding: 9px 22px; font-size: 13px;"
      "}"
      "QPushButton:hover { border-color: #475569; color: #94a3b8; }"
      "QPushButton:pressed { background-color: #1e2433; }");
  settingsBtnRow->addWidget(goSettingsBtn);
  declinedLayout->addLayout(settingsBtnRow);

  consentCardStack->addWidget(declinedPage); // index 1

  connect(acceptBtn, &QPushButton::clicked, this,
          &TradesWidget::onConsentAccepted);
  connect(declineBtn, &QPushButton::clicked, this,
          &TradesWidget::onConsentDeclined);
  connect(goSettingsBtn, &QPushButton::clicked, this,
          &TradesWidget::navigateToSettings);

  hLayout->addWidget(card);
  hLayout->addStretch();
  outerLayout->addLayout(hLayout);
  outerLayout->addStretch();

  return page;
}

void TradesWidget::onConsentAccepted() {
  QSettings settings("CS2Vault", "Settings");
  settings.setValue("trades/consentAccepted", true);
  consentAccepted = true;
  consentStack->setCurrentIndex(1);
  if (companion->isGCReady())
    companion->requestTradeOffers();
}

void TradesWidget::onConsentDeclined() {
  consentCardStack->setCurrentIndex(1);
}

void TradesWidget::resetConsent() {
  QSettings settings("CS2Vault", "Settings");
  settings.setValue("trades/consentAccepted", false);
  consentAccepted = false;
  consentCardStack->setCurrentIndex(0);
  consentStack->setCurrentIndex(0);
}

void TradesWidget::onTradeOffersReceived(
    const QList<TradeOfferData> &offers) {
  // Remove only active (non-resolved) cards — keep resolved cards in place
  // so the user's history is preserved across refreshes.
  QList<TradeOfferCard> kept;
  for (auto &card : cards) {
    if (card.resolved || card.pendingConfirmation) {
      kept.append(card);
    } else {
      if (card.frame) {
        cardLayout->removeWidget(card.frame);
        card.frame->deleteLater();
      }
    }
  }
  cards = kept;

  // Build set of offer IDs already shown (resolved cards we just kept)
  QSet<QString> knownIds;
  for (const auto &card : cards)
    knownIds.insert(card.offerId);

  // Insert new active offers at the top
  int insertPos = 0;
  for (const auto &offer : offers) {
    if (knownIds.contains(offer.id))
      continue;

    QFrame *frame = createOfferCard(offer);
    cardLayout->insertWidget(insertPos++, frame);

    TradeOfferCard card;
    card.offerId = offer.id;
    card.offer = offer;
    card.frame = frame;
    card.timestampLabel = frame->findChild<QLabel *>("timestampLabel");
    card.statusBadge = frame->findChild<QLabel *>("statusBadge");
    card.plLabel = frame->findChild<QLabel *>("plLabel");
    card.acceptButton = frame->findChild<QPushButton *>("acceptButton");
    card.declineButton = frame->findChild<QPushButton *>("declineButton");
    card.cancelButton = frame->findChild<QPushButton *>("cancelButton");
    card.statusMessageLabel = frame->findChild<QLabel *>("statusMessageLabel");
    card.dismissButton = frame->findChild<QPushButton *>("dismissButton");

    // Handle already-resolved offers returned by Steam (e.g. recent history)
    if (offer.state != ETradeOfferState::Active &&
        offer.state != ETradeOfferState::NeedsConfirmation) {
      card.resolved = true;
      QString badgeText;
      QString badgeColor;
      switch (offer.state) {
      case ETradeOfferState::Accepted:
      case ETradeOfferState::InEscrow:
        badgeText = "Accepted";
        badgeColor = "#81c784";
        break;
      case ETradeOfferState::Declined:
        badgeText = "Declined";
        badgeColor = "#e57373";
        break;
      case ETradeOfferState::Canceled:
      case ETradeOfferState::CanceledBySecondFactor:
        badgeText = "Cancelled";
        badgeColor = "#ffb74d";
        break;
      case ETradeOfferState::Expired:
        badgeText = "Expired";
        badgeColor = "#78909c";
        break;
      default:
        badgeText = "Inactive";
        badgeColor = "#78909c";
        break;
      }
      if (card.statusBadge) {
        card.statusBadge->setText(badgeText);
        card.statusBadge->setStyleSheet(
            QString("background-color: %1; color: #0f1117; "
                    "border-radius: 8px; padding: 2px 10px; "
                    "font-size: 11px; font-weight: bold;")
                .arg(badgeColor));
        card.statusBadge->show();
      }
      if (card.acceptButton) card.acceptButton->hide();
      if (card.declineButton) card.declineButton->hide();
      if (card.cancelButton) card.cancelButton->hide();
      if (card.dismissButton) card.dismissButton->show();
      frame->setEnabled(false);
    }

    cards.insert(insertPos - 1, card);
  }
}

void TradesWidget::onNewTradeOffer(const TradeOfferData &offer) {
  QFrame *frame = createOfferCard(offer);
  // Insert at the top (before all other cards, but still before stretch)
  cardLayout->insertWidget(0, frame);

  TradeOfferCard card;
  card.offerId = offer.id;
  card.offer = offer;
  card.frame = frame;
  card.timestampLabel = frame->findChild<QLabel *>("timestampLabel");
  card.statusBadge = frame->findChild<QLabel *>("statusBadge");
  card.plLabel = frame->findChild<QLabel *>("plLabel");
  card.acceptButton = frame->findChild<QPushButton *>("acceptButton");
  card.declineButton = frame->findChild<QPushButton *>("declineButton");
  card.cancelButton = frame->findChild<QPushButton *>("cancelButton");
  card.statusMessageLabel = frame->findChild<QLabel *>("statusMessageLabel");
  card.dismissButton = frame->findChild<QPushButton *>("dismissButton");
  cards.prepend(card);
}

void TradesWidget::onTradeOfferAccepted(const QString &offerId,
                                        const QString &status,
                                        const QString &errorMessage) {
  if (status == "accepted") {
    for (auto &card : cards) {
      if (card.offerId == offerId && !card.resolved) {
        logTradeToHistory(card.offer);
        break;
      }
    }
    resolveCard(offerId, "Accepted", "#81c784");

  } else if (status == "escrow") {
    for (auto &card : cards) {
      if (card.offerId == offerId && !card.resolved) {
        logTradeToHistory(card.offer);
        if (card.statusMessageLabel) {
          card.statusMessageLabel->setText(
              "Items will be held by Steam for up to 15 days");
          card.statusMessageLabel->show();
        }
        break;
      }
    }
    resolveCard(offerId, "In Escrow", "#ffb74d");

  } else if (status == "pending") {
    for (auto &card : cards) {
      if (card.offerId == offerId && !card.resolved && !card.pendingConfirmation) {
        card.pendingConfirmation = true;
        if (card.statusBadge) {
          card.statusBadge->setText("Confirm in Steam App");
          card.statusBadge->setStyleSheet(
              "background-color: #ffb74d; color: #0f1117; "
              "border-radius: 8px; padding: 2px 10px; "
              "font-size: 11px; font-weight: bold;");
          card.statusBadge->show();
        }
        if (card.statusMessageLabel) {
          card.statusMessageLabel->setText(
              "Open your Steam mobile app and confirm this trade\n"
              "under Menu \u2192 Trade Offers \u2192 Confirmation");
          card.statusMessageLabel->show();
        }
        if (card.acceptButton)
          card.acceptButton->hide();
        if (card.declineButton)
          card.declineButton->hide();
        if (card.cancelButton)
          card.cancelButton->hide();
        if (card.frame)
          card.frame->setEnabled(false);
        break;
      }
    }
    // Do not log to history — not final until confirmed via sentOfferChanged

  } else if (status == "error") {
    for (auto &card : cards) {
      if (card.offerId == offerId) {
        if (card.statusBadge) {
          card.statusBadge->setText("Failed");
          card.statusBadge->setStyleSheet(
              "background-color: #c62828; color: #e2e8f0; "
              "border-radius: 8px; padding: 2px 10px; "
              "font-size: 11px; font-weight: bold;");
          card.statusBadge->show();
        }
        if (card.statusMessageLabel) {
          card.statusMessageLabel->setText(errorMessage);
          card.statusMessageLabel->setStyleSheet(
              "font-size: 11px; color: #e57373; font-style: italic;");
          card.statusMessageLabel->show();
        }
        // Re-enable Accept so the user can retry
        if (card.acceptButton)
          card.acceptButton->setEnabled(true);
        if (card.declineButton)
          card.declineButton->setEnabled(true);
        break;
      }
    }
  }
}

void TradesWidget::onTradeOfferCancelled(const QString &offerId,
                                         const QString &status,
                                         const QString &errorMessage) {
  if (status == "error") {
    for (auto &card : cards) {
      if (card.offerId == offerId) {
        if (card.statusBadge) {
          card.statusBadge->setText("Failed");
          card.statusBadge->setStyleSheet(
              "background-color: #c62828; color: #e2e8f0; "
              "border-radius: 8px; padding: 2px 10px; "
              "font-size: 11px; font-weight: bold;");
          card.statusBadge->show();
        }
        if (card.statusMessageLabel) {
          card.statusMessageLabel->setText(errorMessage);
          card.statusMessageLabel->setStyleSheet(
              "font-size: 11px; color: #e57373; font-style: italic;");
          card.statusMessageLabel->show();
        }
        if (card.acceptButton) card.acceptButton->setEnabled(true);
        if (card.declineButton) card.declineButton->setEnabled(true);
        if (card.cancelButton) card.cancelButton->setEnabled(true);
        break;
      }
    }
  } else {
    resolveCard(offerId, "Cancelled", "#ffb74d");
  }
}

void TradesWidget::onTradeOfferChanged(const QString &offerId, int newState) {
  QString badgeText;
  QString badgeColor;
  bool shouldLog = false;

  switch (newState) {
  case ETradeOfferState::Accepted:
  case ETradeOfferState::InEscrow:
    badgeText = "Accepted";
    badgeColor = "#81c784";
    shouldLog = true;
    break;
  case ETradeOfferState::Declined:
    badgeText = "Declined";
    badgeColor = "#e57373";
    break;
  case ETradeOfferState::Canceled:
  case ETradeOfferState::CanceledBySecondFactor:
    badgeText = "Cancelled";
    badgeColor = "#ffb74d";
    break;
  case ETradeOfferState::Expired:
    badgeText = "Expired";
    badgeColor = "#78909c";
    break;
  default:
    return;
  }

  if (shouldLog) {
    for (auto &card : cards) {
      if (card.offerId == offerId && !card.resolved) {
        logTradeToHistory(card.offer);
        break;
      }
    }
  }

  resolveCard(offerId, badgeText, badgeColor);
}

QFrame *TradesWidget::createOfferCard(const TradeOfferData &offer) {
  auto *frame = new QFrame(this);
  frame->setStyleSheet(
      "QFrame#tradeCard { background-color: #151821; border: 1px solid "
      "#1e2433; border-radius: 10px; }");
  frame->setObjectName("tradeCard");

  auto *cardVLayout = new QVBoxLayout(frame);
  cardVLayout->setContentsMargins(16, 14, 16, 14);
  cardVLayout->setSpacing(10);

  // ── Header row ──
  auto *headerRow = new QHBoxLayout();
  headerRow->setSpacing(8);

  QString partnerDisplay = offer.partnerSteamId;
  if (partnerDisplay.length() > 12)
    partnerDisplay = partnerDisplay.left(8) + "...";

  auto *partnerLabel = new QLabel(partnerDisplay, frame);
  partnerLabel->setStyleSheet(
      "font-size: 14px; font-weight: bold; color: #e2e8f0;");
  headerRow->addWidget(partnerLabel);

  auto *timestampLabel = new QLabel(relativeTime(offer.timeCreated), frame);
  timestampLabel->setObjectName("timestampLabel");
  timestampLabel->setStyleSheet("font-size: 11px; color: #64748b;");
  headerRow->addWidget(timestampLabel);

  headerRow->addStretch();

  // Direction badge
  auto *dirBadge = new QLabel(offer.isOurOffer ? "Outgoing" : "Incoming",
                              frame);
  QString dirColor = offer.isOurOffer ? "#ffb74d" : "#4fc3f7";
  dirBadge->setStyleSheet(
      QString("background-color: %1; color: #0f1117; border-radius: 8px; "
              "padding: 2px 10px; font-size: 11px; font-weight: bold;")
          .arg(dirColor));
  headerRow->addWidget(dirBadge);

  // Status badge (hidden initially, shown when resolved)
  auto *statusBadge = new QLabel("", frame);
  statusBadge->setObjectName("statusBadge");
  statusBadge->hide();
  headerRow->addWidget(statusBadge);

  // Dismiss button (hidden until resolved)
  auto *dismissBtn = new QPushButton("×", frame);
  dismissBtn->setObjectName("dismissButton");
  dismissBtn->setFixedSize(22, 22);
  dismissBtn->setStyleSheet(
      "QPushButton { background-color: transparent; color: #475569; "
      "border: none; border-radius: 4px; font-size: 16px; padding: 0; }"
      "QPushButton:hover { color: #e57373; }");
  dismissBtn->setCursor(Qt::PointingHandCursor);
  dismissBtn->hide();
  connect(dismissBtn, &QPushButton::clicked, this,
          [this, id = offer.id]() { dismissCard(id); });
  headerRow->addWidget(dismissBtn);

  cardVLayout->addLayout(headerRow);

  // ── Items row — two columns ──
  auto *itemsRow = new QHBoxLayout();
  itemsRow->setSpacing(16);

  auto *giveCol = createItemColumn("You give", offer.itemsToGive);
  auto *receiveCol = createItemColumn("You receive", offer.itemsToReceive);
  itemsRow->addWidget(giveCol, 1);
  itemsRow->addWidget(receiveCol, 1);
  cardVLayout->addLayout(itemsRow);

  // ── P/L row ──
  double giveTotal = 0.0, receiveTotal = 0.0;
  bool allPriced = true;
  for (const auto &item : offer.itemsToGive) {
    double p = itemPrice(item.marketHashName);
    if (p <= 0.0)
      allPriced = false;
    giveTotal += p;
  }
  for (const auto &item : offer.itemsToReceive) {
    double p = itemPrice(item.marketHashName);
    if (p <= 0.0)
      allPriced = false;
    receiveTotal += p;
  }

  double pl = receiveTotal - giveTotal;
  QString plText;
  if (giveTotal <= 0.0 && receiveTotal <= 0.0) {
    plText = "Profit / Loss: N/A";
  } else {
    QString prefix = allPriced ? "" : "~";
    QString sign = pl >= 0 ? "+" : "";
    plText = QString("Profit / Loss: %1%2$%3")
                 .arg(prefix, sign)
                 .arg(qAbs(pl), 0, 'f', 2);
  }

  auto *plLabel = new QLabel(plText, frame);
  plLabel->setObjectName("plLabel");
  QString plColor =
      (pl >= 0) ? "#81c784" : "#e57373";
  if (giveTotal <= 0.0 && receiveTotal <= 0.0)
    plColor = "#64748b";
  plLabel->setStyleSheet(
      QString("font-size: 13px; font-weight: bold; color: %1;").arg(plColor));
  cardVLayout->addWidget(plLabel);

  // ── Message row ──
  if (!offer.message.isEmpty()) {
    auto *msgLabel = new QLabel(offer.message, frame);
    msgLabel->setStyleSheet(
        "font-size: 12px; color: #64748b; font-style: italic;");
    msgLabel->setWordWrap(true);
    cardVLayout->addWidget(msgLabel);
  }

  // ── Action buttons ──
  auto *btnRow = new QHBoxLayout();
  btnRow->setSpacing(8);
  btnRow->addStretch();

  if (!offer.isOurOffer) {
    auto *acceptBtn = new QPushButton("Accept", frame);
    acceptBtn->setObjectName("acceptButton");
    acceptBtn->setStyleSheet(
        "QPushButton { background-color: #2e7d32; color: #e2e8f0; "
        "border: none; border-radius: 6px; padding: 6px 18px; "
        "font-weight: bold; }"
        "QPushButton:hover { background-color: #388e3c; }"
        "QPushButton:disabled { background-color: #1e2433; color: #334155; }");
    acceptBtn->setCursor(Qt::PointingHandCursor);
    connect(acceptBtn, &QPushButton::clicked, this, [this, id = offer.id]() {
      // Find and disable buttons
      for (auto &card : cards) {
        if (card.offerId == id) {
          if (card.acceptButton)
            card.acceptButton->setEnabled(false);
          if (card.declineButton)
            card.declineButton->setEnabled(false);
          break;
        }
      }
      companion->acceptTradeOffer(id);
    });
    btnRow->addWidget(acceptBtn);

    auto *declineBtn = new QPushButton("Decline", frame);
    declineBtn->setObjectName("declineButton");
    declineBtn->setStyleSheet(
        "QPushButton { background-color: #c62828; color: #e2e8f0; "
        "border: none; border-radius: 6px; padding: 6px 18px; "
        "font-weight: bold; }"
        "QPushButton:hover { background-color: #d32f2f; }"
        "QPushButton:disabled { background-color: #1e2433; color: #334155; }");
    declineBtn->setCursor(Qt::PointingHandCursor);
    connect(declineBtn, &QPushButton::clicked, this, [this, id = offer.id]() {
      for (auto &card : cards) {
        if (card.offerId == id) {
          if (card.acceptButton)
            card.acceptButton->setEnabled(false);
          if (card.declineButton)
            card.declineButton->setEnabled(false);
          break;
        }
      }
      companion->cancelTradeOffer(id);
    });
    btnRow->addWidget(declineBtn);
  } else {
    auto *cancelBtn = new QPushButton("Cancel", frame);
    cancelBtn->setObjectName("cancelButton");
    cancelBtn->setStyleSheet(
        "QPushButton { background-color: #c62828; color: #e2e8f0; "
        "border: none; border-radius: 6px; padding: 6px 18px; "
        "font-weight: bold; }"
        "QPushButton:hover { background-color: #d32f2f; }"
        "QPushButton:disabled { background-color: #1e2433; color: #334155; }");
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, this, [this, id = offer.id]() {
      for (auto &card : cards) {
        if (card.offerId == id) {
          if (card.cancelButton)
            card.cancelButton->setEnabled(false);
          break;
        }
      }
      companion->cancelTradeOffer(id);
    });
    btnRow->addWidget(cancelBtn);
  }

  cardVLayout->addLayout(btnRow);

  // ── Status message row (escrow note / pending instructions / error text) ──
  auto *statusMsgLabel = new QLabel("", frame);
  statusMsgLabel->setObjectName("statusMessageLabel");
  statusMsgLabel->setStyleSheet(
      "font-size: 11px; color: #94a3b8; font-style: italic;");
  statusMsgLabel->setWordWrap(true);
  statusMsgLabel->hide();
  cardVLayout->addWidget(statusMsgLabel);

  // ── History status row ──
  auto *historyHint =
      new QLabel("Will be logged to trade history", frame);
  historyHint->setStyleSheet(
      "font-size: 11px; color: #475569; font-style: italic;");
  cardVLayout->addWidget(historyHint);

  return frame;
}

QWidget *TradesWidget::createItemColumn(const QString &title,
                                        const QList<TradeItem> &items) {
  auto *col = new QWidget(this);
  auto *layout = new QVBoxLayout(col);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);

  auto *titleLabel = new QLabel(title, col);
  titleLabel->setStyleSheet(
      "font-size: 11px; font-weight: bold; color: #64748b; "
      "text-transform: uppercase; letter-spacing: 0.5px;");
  layout->addWidget(titleLabel);

  double total = 0.0;
  for (const auto &item : items) {
    auto *row = new QHBoxLayout();
    row->setSpacing(6);

    auto *nameLabel = new QLabel(item.marketHashName, col);
    nameLabel->setWordWrap(true);

    // Color by rarity if available
    QString nameColor = "#e2e8f0";
    if (itemDb && itemDb->isLoaded()) {
      ItemInfo info = itemDb->lookup(item.marketHashName);
      if (info.rarityColor.isValid())
        nameColor = info.rarityColor.name();
    }
    nameLabel->setStyleSheet(
        QString("font-size: 12px; color: %1;").arg(nameColor));
    row->addWidget(nameLabel, 1);

    double price = itemPrice(item.marketHashName);
    total += price;
    auto *priceLabel = new QLabel(
        price > 0 ? QString("$%1").arg(price, 0, 'f', 2) : "—", col);
    priceLabel->setStyleSheet("font-size: 12px; color: #94a3b8; "
                              "font-family: 'JetBrains Mono';");
    priceLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(priceLabel);

    layout->addLayout(row);
  }

  if (items.isEmpty()) {
    auto *emptyLabel = new QLabel("No items", col);
    emptyLabel->setStyleSheet("font-size: 12px; color: #475569;");
    layout->addWidget(emptyLabel);
  }

  // Total
  auto *sep = new QFrame(col);
  sep->setFrameShape(QFrame::HLine);
  sep->setStyleSheet("background-color: #1e2433; max-height: 1px;");
  layout->addWidget(sep);

  auto *totalLabel = new QLabel(
      total > 0 ? QString("Total: $%1").arg(total, 0, 'f', 2) : "Total: —",
      col);
  totalLabel->setStyleSheet("font-size: 12px; font-weight: bold; "
                            "color: #94a3b8; font-family: 'JetBrains Mono';");
  totalLabel->setAlignment(Qt::AlignRight);
  layout->addWidget(totalLabel);

  return col;
}

void TradesWidget::updateTimestamps() {
  for (const auto &card : cards) {
    if (card.timestampLabel)
      card.timestampLabel->setText(relativeTime(card.offer.timeCreated));
  }
}

void TradesWidget::resolveCard(const QString &offerId,
                               const QString &badgeText,
                               const QString &badgeColor) {
  for (auto &card : cards) {
    if (card.offerId == offerId) {
      card.resolved = true;
      if (card.statusBadge) {
        card.statusBadge->setText(badgeText);
        card.statusBadge->setStyleSheet(
            QString("background-color: %1; color: #0f1117; "
                    "border-radius: 8px; padding: 2px 10px; "
                    "font-size: 11px; font-weight: bold;")
                .arg(badgeColor));
        card.statusBadge->show();
      }
      if (card.acceptButton)
        card.acceptButton->hide();
      if (card.declineButton)
        card.declineButton->hide();
      if (card.cancelButton)
        card.cancelButton->hide();
      if (card.dismissButton)
        card.dismissButton->show();
      if (card.frame)
        card.frame->setEnabled(false);
      break;
    }
  }
}

void TradesWidget::logTradeToHistory(const TradeOfferData &offer) {
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  QString notes =
      QString("Via trade with %1").arg(offer.partnerSteamId);

  for (const auto &item : offer.itemsToGive) {
    TradeHistoryEntry entry;
    entry.itemName = item.marketHashName;
    entry.type = "traded_away";
    entry.timestamp = now;
    entry.notes = notes;
    double p = itemPrice(item.marketHashName);
    if (p > 0.0)
      entry.sellPrice = p;
    tradeHistoryManager->addEntry(entry);
  }

  for (const auto &item : offer.itemsToReceive) {
    TradeHistoryEntry entry;
    entry.itemName = item.marketHashName;
    entry.type = "acquired";
    entry.timestamp = now;
    entry.notes = notes;
    double p = itemPrice(item.marketHashName);
    if (p > 0.0)
      entry.buyPrice = p;
    tradeHistoryManager->addEntry(entry);
  }
}

void TradesWidget::dismissCard(const QString &offerId) {
  for (int i = 0; i < cards.size(); ++i) {
    if (cards[i].offerId == offerId) {
      if (cards[i].frame) {
        cardLayout->removeWidget(cards[i].frame);
        cards[i].frame->deleteLater();
      }
      cards.removeAt(i);
      break;
    }
  }
}

QString TradesWidget::relativeTime(qint64 unixTimestamp) const {
  if (unixTimestamp <= 0)
    return "";
  qint64 nowSecs = QDateTime::currentSecsSinceEpoch();
  qint64 diff = nowSecs - unixTimestamp;
  if (diff < 0)
    diff = 0;

  if (diff < 60)
    return "just now";
  if (diff < 3600) {
    int mins = static_cast<int>(diff / 60);
    return QString("%1 minute%2 ago").arg(mins).arg(mins == 1 ? "" : "s");
  }
  if (diff < 86400) {
    int hours = static_cast<int>(diff / 3600);
    return QString("%1 hour%2 ago").arg(hours).arg(hours == 1 ? "" : "s");
  }
  int days = static_cast<int>(diff / 86400);
  return QString("%1 day%2 ago").arg(days).arg(days == 1 ? "" : "s");
}

double TradesWidget::itemPrice(const QString &marketHashName) const {
  if (!api || !api->arePricesLoaded())
    return 0.0;
  return api->fetchPrice(marketHashName);
}

void TradesWidget::onFamilyViewRequired(const QString &offerId) {
  pendingFamilyViewOfferId = offerId;

  if (familyViewUnlocked) {
    companion->acceptTradeOffer(offerId);
    return;
  }

  // Don't open a second dialog if one is already showing
  if (pinDialog)
    return;

  pinDialog = new QDialog(this);
  pinDialog->setWindowTitle("Family View PIN");
  pinDialog->setModal(true);

  auto *layout = new QVBoxLayout(pinDialog);
  layout->setSpacing(12);

  auto *infoLabel = new QLabel(
      "Family View is enabled on this account.\n"
      "Enter your PIN to accept this trade.",
      pinDialog);
  infoLabel->setWordWrap(true);
  layout->addWidget(infoLabel);

  auto *pinEdit = new QLineEdit(pinDialog);
  pinEdit->setEchoMode(QLineEdit::Password);
  pinEdit->setMaxLength(4);
  pinEdit->setPlaceholderText("PIN");
  auto *validator = new QRegularExpressionValidator(
      QRegularExpression("[0-9]{0,4}"), pinEdit);
  pinEdit->setValidator(validator);
  layout->addWidget(pinEdit);

  pinErrorLabel = new QLabel("", pinDialog);
  pinErrorLabel->setStyleSheet("color: #e57373;");
  pinErrorLabel->hide();
  layout->addWidget(pinErrorLabel);

  auto *btnRow = new QHBoxLayout();
  btnRow->addStretch();
  auto *unlockBtn = new QPushButton("Unlock & Accept", pinDialog);
  unlockBtn->setObjectName("unlockButton");
  auto *cancelBtn = new QPushButton("Cancel", pinDialog);
  btnRow->addWidget(unlockBtn);
  btnRow->addWidget(cancelBtn);
  layout->addLayout(btnRow);

  connect(unlockBtn, &QPushButton::clicked, this,
          [this, pinEdit, unlockBtn]() {
            if (pinEdit->text().isEmpty())
              return;
            unlockBtn->setEnabled(false);
            companion->unlockParentalView(pinEdit->text());
          });

  connect(cancelBtn, &QPushButton::clicked, this, [this, offerId]() {
    pinDialog->close();
    pinDialog = nullptr;
    pinErrorLabel = nullptr;
    // Re-enable the Accept/Decline buttons so the user can retry manually
    for (auto &card : cards) {
      if (card.offerId == offerId) {
        if (card.acceptButton)
          card.acceptButton->setEnabled(true);
        if (card.declineButton)
          card.declineButton->setEnabled(true);
        break;
      }
    }
  });

  pinDialog->show();
}

void TradesWidget::onParentalUnlockResult(bool success, const QString &error) {
  if (success) {
    familyViewUnlocked = true;
    if (pinDialog) {
      pinDialog->close();
      pinDialog = nullptr;
      pinErrorLabel = nullptr;
    }
    if (!pendingFamilyViewOfferId.isEmpty())
      companion->acceptTradeOffer(pendingFamilyViewOfferId);
  } else {
    if (pinErrorLabel) {
      pinErrorLabel->setText(error.isEmpty() ? "Incorrect PIN. Please try again."
                                             : error);
      pinErrorLabel->show();
    }
    // Re-enable the Unlock button so the user can retry
    if (pinDialog) {
      auto *unlockBtn = pinDialog->findChild<QPushButton *>("unlockButton");
      if (unlockBtn)
        unlockBtn->setEnabled(true);
    }
  }
}

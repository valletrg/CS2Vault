#include "loginwindow.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

static const QString WINDOW_STYLE = R"(
    LoginWindow {
        background-color: #16181e;
    }
    QLabel {
        color: #e0e0e0;
    }
    QPushButton#primary {
        background-color: #1b88d4;
        color: white;
        border: none;
        border-radius: 6px;
        padding: 10px 20px;
        font-size: 14px;
        font-weight: bold;
    }
    QPushButton#primary:hover { background-color: #2299e8; }
    QPushButton#primary:disabled { background-color: #3a3d4a; color: #666; }
    QPushButton#secondary {
        background-color: #2a2d38;
        color: #ccc;
        border: 1px solid #3a3d4a;
        border-radius: 6px;
        padding: 10px 20px;
        font-size: 13px;
    }
    QPushButton#secondary:hover { background-color: #353849; }
    QPushButton#back {
        background-color: transparent;
        color: #555;
        border: none;
        font-size: 12px;
        text-decoration: underline;
    }
    QPushButton#back:hover { color: #888; }
    QLineEdit {
        background: #1e2130;
        color: #e0e0e0;
        border: 1px solid #3a3d4a;
        border-radius: 6px;
        padding: 8px;
        font-size: 13px;
    }
    QLineEdit:focus { border-color: #1b88d4; }
    QProgressBar {
        border: none;
        background: #2a2d38;
        border-radius: 3px;
        height: 4px;
    }
    QProgressBar::chunk { background: #1b88d4; border-radius: 3px; }

    QPushButton#sourceCard {
        background-color: #1e2130;
        color: #ccc;
        border: 2px solid #2a2d38;
        border-radius: 10px;
        padding: 14px 10px;
        font-size: 13px;
        text-align: left;
    }
    QPushButton#sourceCard:hover {
        border-color: #1b88d4;
        background-color: #222538;
        color: #e0e0e0;
    }
    QPushButton#sourceCardSelected {
        background-color: #1a2840;
        color: #e0e0e0;
        border: 2px solid #1b88d4;
        border-radius: 10px;
        padding: 14px 10px;
        font-size: 13px;
        text-align: left;
    }
)";

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

LoginWindow::LoginWindow(QWidget *parent)
    : QWidget(parent), companion(new SteamCompanion(this)),
      nam(new QNetworkAccessManager(this)) {
  setupUI();
  setStyleSheet(WINDOW_STYLE);

  connect(companion, &SteamCompanion::qrCodeReady, this,
          [this](const QString &url) {
            QUrl apiUrl("https://api.qrserver.com/v1/create-qr-code/");
            QUrlQuery query;
            query.addQueryItem("size", "220x220");
            query.addQueryItem("data", url);
            apiUrl.setQuery(query);

            QNetworkReply *reply = nam->get(QNetworkRequest(apiUrl));
            connect(reply, &QNetworkReply::finished, this, [this, reply]() {
              reply->deleteLater();
              if (reply->error() == QNetworkReply::NoError) {
                QPixmap px;
                px.loadFromData(reply->readAll());
                if (!px.isNull())
                  qrImageLabel->setPixmap(px.scaled(
                      220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation));
              } else {
                qrImageLabel->setText(
                    "Could not load QR.\nCheck internet connection.");
              }
            });

            showQRPage();
            qrStatusLabel->setText("Waiting for scan...");
            qrProgressBar->setRange(0, 0);
          });

  connect(companion, &SteamCompanion::qrScanned, this, [this]() {
    qrStatusLabel->setText("QR scanned — approve in Steam app...");
    qrStatusLabel->setStyleSheet("color: #F0A500; font-size: 11px;");
  });

  connect(companion, &SteamCompanion::loggedIn, this,
          [this](const QString &) { handleLoginSuccess(); });

  connect(companion, &SteamCompanion::statusMessage, this,
          [this](const QString &msg) {
            if (stack->currentWidget() == qrPage)
              qrStatusLabel->setText(msg);
            else if (stack->currentWidget() == tokenPage)
              tokenStatusLabel->setText(msg);
          });

  connect(companion, &SteamCompanion::errorOccurred, this,
          [this](const QString &err) {
            if (stack->currentWidget() == qrPage) {
              qrStatusLabel->setText("Error: " + err);
              qrStatusLabel->setStyleSheet("color: #DC4646; font-size: 11px;");
              qrProgressBar->setRange(0, 1);
              qrProgressBar->setValue(0);
              qrBackButton->setEnabled(true);
            } else if (stack->currentWidget() == tokenPage) {
              tokenStatusLabel->setText("Error: " + err);
              tokenStatusLabel->setStyleSheet(
                  "color: #DC4646; font-size: 11px;");
              tokenProgressBar->setRange(0, 1);
              tokenProgressBar->setValue(0);
              tokenSubmitButton->setEnabled(true);
              tokenBackButton->setEnabled(true);
            } else {
              showWelcomePage();
            }
          });
}

LoginWindow::~LoginWindow() = default;

SteamCompanion *LoginWindow::takeCompanion() {
  SteamCompanion *c = companion;
  companion = nullptr;
  c->setParent(nullptr);
  return c;
}

void LoginWindow::tryAutoLogin() {
  QStringList paths = {QCoreApplication::applicationDirPath() +
                           "/steamcompanion/refresh_token.txt",
                       QCoreApplication::applicationDirPath() +
                           "/../../steamcompanion/refresh_token.txt"};
  for (const QString &p : paths) {
    if (QFile::exists(p)) {
      companion->start();
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void LoginWindow::setupUI() {
  setWindowTitle("CS2Vault — Sign in");
  setFixedSize(440, 540);

  QVBoxLayout *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);

  stack = new QStackedWidget(this);
  root->addWidget(stack);

  setupWelcomePage();
  setupQRPage();
  setupTokenPage();
  setupOnboardingPage();

  stack->addWidget(welcomePage);
  stack->addWidget(qrPage);
  stack->addWidget(tokenPage);
  stack->addWidget(onboardingPage);

  showWelcomePage();
}

void LoginWindow::setupWelcomePage() {
  welcomePage = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(welcomePage);
  layout->setContentsMargins(40, 40, 40, 40);
  layout->setSpacing(16);
  layout->setAlignment(Qt::AlignCenter);

  QLabel *logo = new QLabel("CS2Vault", welcomePage);
  QFont titleFont;
  titleFont.setPointSize(24);
  titleFont.setBold(true);
  logo->setFont(titleFont);
  logo->setAlignment(Qt::AlignCenter);
  logo->setStyleSheet("color: #1b88d4;");
  layout->addWidget(logo);

  QLabel *subtitle = new QLabel("Sign in to get started", welcomePage);
  subtitle->setAlignment(Qt::AlignCenter);
  subtitle->setStyleSheet("color: #666; font-size: 13px;");
  layout->addWidget(subtitle);

  layout->addSpacing(20);

  qrLoginButton = new QPushButton("Sign in with Steam QR", welcomePage);
  qrLoginButton->setObjectName("primary");
  qrLoginButton->setFixedHeight(50);
  layout->addWidget(qrLoginButton);

  QLabel *qrHint =
      new QLabel("Scan a QR code with your Steam mobile app", welcomePage);
  qrHint->setAlignment(Qt::AlignCenter);
  qrHint->setStyleSheet("color: #555; font-size: 11px;");
  layout->addWidget(qrHint);

  layout->addSpacing(8);

  tokenLoginButton = new QPushButton("Sign in via Browser Token", welcomePage);
  tokenLoginButton->setObjectName("secondary");
  tokenLoginButton->setFixedHeight(50);
  layout->addWidget(tokenLoginButton);

  QLabel *tokenHint =
      new QLabel("Already logged in to Steam in your browser?", welcomePage);
  tokenHint->setAlignment(Qt::AlignCenter);
  tokenHint->setStyleSheet("color: #555; font-size: 11px;");
  layout->addWidget(tokenHint);

  layout->addStretch();

  connect(qrLoginButton, &QPushButton::clicked, this,
          &LoginWindow::onStartLogin);
  connect(tokenLoginButton, &QPushButton::clicked, this,
          &LoginWindow::showTokenPage);
}

void LoginWindow::setupQRPage() {
  qrPage = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(qrPage);
  layout->setContentsMargins(40, 32, 40, 32);
  layout->setSpacing(12);
  layout->setAlignment(Qt::AlignCenter);

  QLabel *title = new QLabel("Scan with Steam App", qrPage);
  QFont f;
  f.setPointSize(16);
  f.setBold(true);
  title->setFont(f);
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);

  qrInstructionLabel =
      new QLabel("Open Steam → Menu → Sign in via QR code", qrPage);
  qrInstructionLabel->setAlignment(Qt::AlignCenter);
  qrInstructionLabel->setStyleSheet("color: #888; font-size: 12px;");
  layout->addWidget(qrInstructionLabel);

  qrImageLabel = new QLabel(qrPage);
  qrImageLabel->setFixedSize(220, 220);
  qrImageLabel->setAlignment(Qt::AlignCenter);
  qrImageLabel->setStyleSheet(
      "border: 1px solid #2a2d38; background: #1c1e26; color: #555;");
  qrImageLabel->setText("Loading QR code...");
  layout->addWidget(qrImageLabel, 0, Qt::AlignCenter);

  qrStatusLabel = new QLabel("Starting...", qrPage);
  qrStatusLabel->setAlignment(Qt::AlignCenter);
  qrStatusLabel->setStyleSheet("color: #888; font-size: 11px;");
  layout->addWidget(qrStatusLabel);

  qrProgressBar = new QProgressBar(qrPage);
  qrProgressBar->setRange(0, 1);
  qrProgressBar->setValue(0);
  qrProgressBar->setTextVisible(false);
  qrProgressBar->setFixedHeight(4);
  layout->addWidget(qrProgressBar);

  qrBackButton = new QPushButton("← Use a different sign-in method", qrPage);
  qrBackButton->setObjectName("back");
  layout->addWidget(qrBackButton, 0, Qt::AlignCenter);

  connect(qrBackButton, &QPushButton::clicked, this,
          &LoginWindow::showWelcomePage);
}

void LoginWindow::setupTokenPage() {
  tokenPage = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(tokenPage);
  layout->setContentsMargins(40, 32, 40, 32);
  layout->setSpacing(12);
  layout->setAlignment(Qt::AlignCenter);

  QLabel *title = new QLabel("Browser Token Sign In", tokenPage);
  QFont f;
  f.setPointSize(16);
  f.setBold(true);
  title->setFont(f);
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);

  QLabel *step1 =
      new QLabel("Step 1: Open the token page in your browser", tokenPage);
  step1->setStyleSheet("color: #aaa; font-size: 12px;");
  step1->setAlignment(Qt::AlignCenter);
  layout->addWidget(step1);

  tokenOpenBrowserButton =
      new QPushButton("Open steamcommunity.com/chat/clientjstoken", tokenPage);
  tokenOpenBrowserButton->setObjectName("secondary");
  tokenOpenBrowserButton->setFixedHeight(40);
  layout->addWidget(tokenOpenBrowserButton);

  QLabel *step2 = new QLabel(
      "Step 2: Copy the value next to \"token\" and paste it below", tokenPage);
  step2->setStyleSheet("color: #aaa; font-size: 12px;");
  step2->setAlignment(Qt::AlignCenter);
  step2->setWordWrap(true);
  layout->addWidget(step2);

  tokenPasteEdit = new QLineEdit(tokenPage);
  tokenPasteEdit->setPlaceholderText("Paste token here...");
  tokenPasteEdit->setFixedHeight(40);
  layout->addWidget(tokenPasteEdit);

  tokenSubmitButton = new QPushButton("Sign In", tokenPage);
  tokenSubmitButton->setObjectName("primary");
  tokenSubmitButton->setFixedHeight(44);
  layout->addWidget(tokenSubmitButton);

  tokenStatusLabel = new QLabel("", tokenPage);
  tokenStatusLabel->setAlignment(Qt::AlignCenter);
  tokenStatusLabel->setStyleSheet("color: #888; font-size: 11px;");
  layout->addWidget(tokenStatusLabel);

  tokenProgressBar = new QProgressBar(tokenPage);
  tokenProgressBar->setRange(0, 1);
  tokenProgressBar->setValue(0);
  tokenProgressBar->setTextVisible(false);
  tokenProgressBar->setFixedHeight(4);
  layout->addWidget(tokenProgressBar);

  tokenBackButton =
      new QPushButton("← Use a different sign-in method", tokenPage);
  tokenBackButton->setObjectName("back");
  layout->addWidget(tokenBackButton, 0, Qt::AlignCenter);

  connect(tokenOpenBrowserButton, &QPushButton::clicked, this, []() {
    QDesktopServices::openUrl(
        QUrl("https://steamcommunity.com/chat/clientjstoken"));
  });

  connect(tokenSubmitButton, &QPushButton::clicked, this, [this]() {
    QString token = tokenPasteEdit->text().trimmed();
    if (token.isEmpty()) {
      tokenStatusLabel->setText("Please paste a token first.");
      tokenStatusLabel->setStyleSheet("color: #DC4646; font-size: 11px;");
      return;
    }
    tokenSubmitButton->setEnabled(false);
    tokenBackButton->setEnabled(false);
    tokenStatusLabel->setText("Logging in...");
    tokenStatusLabel->setStyleSheet("color: #5A9BE6; font-size: 11px;");
    tokenProgressBar->setRange(0, 0);

    if (!companion->isRunning())
      companion->start();

    QTimer::singleShot(500, this, [this, token]() {
      companion->sendCommand(
          QJsonObject{{"command", "login_with_web_token"}, {"token", token}});
    });
  });

  connect(tokenPasteEdit, &QLineEdit::returnPressed, tokenSubmitButton,
          &QPushButton::click);
  connect(tokenBackButton, &QPushButton::clicked, this,
          &LoginWindow::showWelcomePage);
}

void LoginWindow::setupOnboardingPage() {
  onboardingPage = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(onboardingPage);
  layout->setContentsMargins(36, 32, 36, 32);
  layout->setSpacing(14);

  // Header
  QLabel *title = new QLabel("Choose a Price Source", onboardingPage);
  QFont f;
  f.setPointSize(16);
  f.setBold(true);
  title->setFont(f);
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);

  QLabel *subtitle = new QLabel(
      "CS2Vault needs a pricing source to show current skin values.\n"
      "You can change this any time in Settings.",
      onboardingPage);
  subtitle->setAlignment(Qt::AlignCenter);
  subtitle->setWordWrap(true);
  subtitle->setStyleSheet("color: #666; font-size: 12px;");
  layout->addWidget(subtitle);

  // ── Source cards ──────────────────────────────────────────────────────

  // CSFloat card
  cardCSFloat = new QPushButton(onboardingPage);
  cardCSFloat->setObjectName("sourceCard");
  cardCSFloat->setText(
      "  CSFloat  \u2605  Recommended\n"
      "  Real western market prices. Free API key — anyone can sign up.");
  cardCSFloat->setFixedHeight(64);
  cardCSFloat->setCheckable(false);
  layout->addWidget(cardCSFloat);

  // Buff163 card
  cardBuff = new QPushButton(onboardingPage);
  cardBuff->setObjectName("sourceCard");
  cardBuff->setText(
      "  Buff163\n"
      "  Best prices globally. Requires an existing Buff163 account.");
  cardBuff->setFixedHeight(64);
  layout->addWidget(cardBuff);

  // Steam card
  cardSteam = new QPushButton(onboardingPage);
  cardSteam->setObjectName("sourceCard");
  cardSteam->setText(
      "  Steam Market  \u2014  Skip for now\n"
      "  No account needed but prices are higher than real market value.");
  cardSteam->setFixedHeight(64);
  layout->addWidget(cardSteam);

  // ── Key / cookie input (shown when CSFloat or Buff selected) ─────────

  keyInputArea = new QWidget(onboardingPage);
  QVBoxLayout *keyLayout = new QVBoxLayout(keyInputArea);
  keyLayout->setContentsMargins(0, 0, 0, 0);
  keyLayout->setSpacing(6);

  keyInputLabel = new QLabel(keyInputArea);
  keyInputLabel->setStyleSheet(
      "color: #ccc; font-size: 12px; font-weight: bold;");

  keyInputEdit = new QLineEdit(keyInputArea);
  keyInputEdit->setFixedHeight(38);
  keyInputEdit->setEchoMode(QLineEdit::Password);

  keyInputHint = new QLabel(keyInputArea);
  keyInputHint->setWordWrap(true);
  keyInputHint->setOpenExternalLinks(true);
  keyInputHint->setStyleSheet("color: #555; font-size: 11px;");

  keyLayout->addWidget(keyInputLabel);
  keyLayout->addWidget(keyInputEdit);
  keyLayout->addWidget(keyInputHint);

  keyInputArea->hide();
  layout->addWidget(keyInputArea);

  // ── Error label ───────────────────────────────────────────────────────

  onboardingErrorLabel = new QLabel(onboardingPage);
  onboardingErrorLabel->setAlignment(Qt::AlignCenter);
  onboardingErrorLabel->setStyleSheet("color: #DC4646; font-size: 11px;");
  onboardingErrorLabel->hide();
  layout->addWidget(onboardingErrorLabel);

  layout->addStretch();

  // ── Get Started button ────────────────────────────────────────────────

  onboardingNextButton = new QPushButton("Get Started", onboardingPage);
  onboardingNextButton->setObjectName("primary");
  onboardingNextButton->setFixedHeight(48);
  onboardingNextButton->setEnabled(false); // enabled once a source is picked
  layout->addWidget(onboardingNextButton);

  // ── Wire up card clicks ───────────────────────────────────────────────

  connect(cardCSFloat, &QPushButton::clicked, this,
          [this]() { selectSource("csfloat"); });
  connect(cardBuff, &QPushButton::clicked, this,
          [this]() { selectSource("buff"); });
  connect(cardSteam, &QPushButton::clicked, this,
          [this]() { selectSource("steam"); });

  // ── Get Started clicked ───────────────────────────────────────────────

  connect(onboardingNextButton, &QPushButton::clicked, this, [this]() {
    // Validate key/cookie if needed
    if ((selectedSource == "csfloat" || selectedSource == "buff") &&
        keyInputEdit->text().trimmed().isEmpty()) {
      onboardingErrorLabel->setText(
          selectedSource == "csfloat"
              ? "Please paste your CSFloat API key, or choose Steam to skip."
              : "Please paste your Buff163 session cookie, or choose Steam to "
                "skip.");
      onboardingErrorLabel->show();
      return;
    }

    // Persist settings
    QSettings settings("CS2Vault", "Settings");
    settings.setValue("onboardingComplete", true);

    if (selectedSource == "csfloat") {
      settings.setValue("priceSource", "CSFloat");
      settings.setValue("csFloatKey", keyInputEdit->text().trimmed());
    } else if (selectedSource == "buff") {
      settings.setValue("priceSource", "Buff163 (coming soon)");
      settings.setValue("buff163Cookie", keyInputEdit->text().trimmed());
    } else {
      settings.setValue("priceSource", "Steam Market");
    }

    emit loginComplete();
  });
}

// ---------------------------------------------------------------------------
// Source card selection
// ---------------------------------------------------------------------------

void LoginWindow::selectSource(const QString &source) {
  selectedSource = source;
  onboardingErrorLabel->hide();

  // Reset all cards to unselected style
  for (QPushButton *card : {cardCSFloat, cardBuff, cardSteam})
    card->setObjectName("sourceCard");

  // Highlight selected card
  if (source == "csfloat")
    cardCSFloat->setObjectName("sourceCardSelected");
  else if (source == "buff")
    cardBuff->setObjectName("sourceCardSelected");
  else if (source == "steam")
    cardSteam->setObjectName("sourceCardSelected");

  // Re-apply stylesheet so Qt picks up the name change
  setStyleSheet(styleSheet());

  // Show/hide key input
  if (source == "csfloat") {
    keyInputLabel->setText("CSFloat API Key");
    keyInputEdit->setPlaceholderText("Paste your API key here...");
    keyInputHint->setText(
        "<a href='https://csfloat.com/profile' style='color:#5b9bd5;'>"
        "Get your free key at csfloat.com/profile</a> → Developer tab.");
    keyInputArea->show();
  } else if (source == "buff") {
    keyInputLabel->setText("Buff163 Session Cookie");
    keyInputEdit->setPlaceholderText("Paste your session cookie here...");
    keyInputHint->setText(
        "Log in to buff163.com in your browser, open DevTools → "
        "Application → Cookies, and copy the value of the 'session' cookie.");
    keyInputArea->show();
  } else {
    keyInputArea->hide();
  }

  onboardingNextButton->setEnabled(true);

  // Resize window slightly if key area is shown
  setFixedSize(440, source == "steam" ? 540 : 620);
}

// ---------------------------------------------------------------------------
// Page transitions
// ---------------------------------------------------------------------------

void LoginWindow::showWelcomePage() {
  qrImageLabel->clear();
  qrImageLabel->setText("Loading QR code...");
  qrImageLabel->setStyleSheet(
      "border: 1px solid #2a2d38; background: #1c1e26; color: #555;");
  qrProgressBar->setRange(0, 1);
  qrProgressBar->setValue(0);
  qrStatusLabel->setText("Starting...");
  qrStatusLabel->setStyleSheet("color: #888; font-size: 11px;");
  qrBackButton->setEnabled(true);

  tokenPasteEdit->clear();
  tokenStatusLabel->clear();
  tokenProgressBar->setRange(0, 1);
  tokenProgressBar->setValue(0);
  tokenSubmitButton->setEnabled(true);
  tokenBackButton->setEnabled(true);

  setFixedSize(440, 540);
  stack->setCurrentWidget(welcomePage);
}

void LoginWindow::showQRPage() { stack->setCurrentWidget(qrPage); }
void LoginWindow::showTokenPage() { stack->setCurrentWidget(tokenPage); }

void LoginWindow::showOnboardingPage() {
  // Reset state
  selectedSource.clear();
  keyInputArea->hide();
  keyInputEdit->clear();
  onboardingErrorLabel->hide();
  onboardingNextButton->setEnabled(false);
  for (QPushButton *card : {cardCSFloat, cardBuff, cardSteam})
    card->setObjectName("sourceCard");
  setStyleSheet(styleSheet());
  setFixedSize(440, 540);

  stack->setCurrentWidget(onboardingPage);
}

// ---------------------------------------------------------------------------
// Login success → decide whether to onboard or go straight to app
// ---------------------------------------------------------------------------

void LoginWindow::handleLoginSuccess() {
  // Show success feedback on the active page
  if (stack->currentWidget() == qrPage) {
    qrImageLabel->setText("\u2713");
    qrImageLabel->setStyleSheet(
        "border: 2px solid #38C878; background: #1c2e1e; "
        "color: #38C878; font-size: 48px;");
    qrStatusLabel->setText("\u2713 Signed in! One more step...");
    qrStatusLabel->setStyleSheet(
        "color: #38C878; font-size: 11px; font-weight: bold;");
    qrProgressBar->setRange(0, 1);
    qrProgressBar->setValue(1);
  } else if (stack->currentWidget() == tokenPage) {
    tokenStatusLabel->setText("\u2713 Signed in! One more step...");
    tokenStatusLabel->setStyleSheet(
        "color: #38C878; font-size: 11px; font-weight: bold;");
    tokenProgressBar->setRange(0, 1);
    tokenProgressBar->setValue(1);
  }

  QTimer::singleShot(800, this, [this]() {
    QSettings settings("CS2Vault", "Settings");
    if (settings.value("onboardingComplete", false).toBool()) {
      // Returning user — go straight to the app
      emit loginComplete();
    } else {
      // First time — show the onboarding wizard
      showOnboardingPage();
    }
  });
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void LoginWindow::onStartLogin() {
  qrLoginButton->setEnabled(false);
  showQRPage();
  qrStatusLabel->setText("Connecting to Steam...");
  qrProgressBar->setRange(0, 0);
  companion->start();
}

void LoginWindow::setStatus(const QString &text, const QString &color) {
  QString style = QString("color: %1; font-size: 11px;").arg(color);
  if (stack->currentWidget() == qrPage) {
    qrStatusLabel->setText(text);
    qrStatusLabel->setStyleSheet(style);
  } else if (stack->currentWidget() == tokenPage) {
    tokenStatusLabel->setText(text);
    tokenStatusLabel->setStyleSheet(style);
  }
}
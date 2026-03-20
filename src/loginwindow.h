#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include "steamcompanion.h"
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QWidget>

class LoginWindow : public QWidget {
  Q_OBJECT

public:
  explicit LoginWindow(QWidget *parent = nullptr);
  ~LoginWindow();

  SteamCompanion *takeCompanion();
  void tryAutoLogin();

signals:
  void loginComplete();

private slots:
  void onStartLogin();

private:
  void setupUI();
  void setupWelcomePage();
  void setupQRPage();
  void setupTokenPage();
  void setupOnboardingPage();

  void showWelcomePage();
  void showQRPage();
  void showTokenPage();
  void showOnboardingPage();

  void setStatus(const QString &text, const QString &color = "gray");
  void handleLoginSuccess();

  // Updates the onboarding UI when a source card is selected.
  void selectSource(const QString &source);

  SteamCompanion *companion;
  QNetworkAccessManager *nam;
  QStackedWidget *stack;

  // ── Welcome page ──────────────────────────
  QWidget *welcomePage = nullptr;
  QPushButton *qrLoginButton = nullptr;
  QPushButton *tokenLoginButton = nullptr;

  // ── QR page ───────────────────────────────
  QWidget *qrPage = nullptr;
  QLabel *qrImageLabel = nullptr;
  QLabel *qrInstructionLabel = nullptr;
  QLabel *qrStatusLabel = nullptr;
  QProgressBar *qrProgressBar = nullptr;
  QPushButton *qrBackButton = nullptr;

  // ── Token page ────────────────────────────
  QWidget *tokenPage = nullptr;
  QLabel *tokenStatusLabel = nullptr;
  QProgressBar *tokenProgressBar = nullptr;
  QPushButton *tokenOpenBrowserButton = nullptr;
  QLineEdit *tokenPasteEdit = nullptr;
  QPushButton *tokenSubmitButton = nullptr;
  QPushButton *tokenBackButton = nullptr;

  // ── Onboarding page ───────────────────────
  QWidget *onboardingPage = nullptr;

  // Source selection cards
  QPushButton *cardCSFloat = nullptr;
  QPushButton *cardBuff = nullptr;
  QPushButton *cardSteam = nullptr;

  // Key / cookie input area (shown/hidden depending on selection)
  QWidget *keyInputArea = nullptr;
  QLabel *keyInputLabel = nullptr;
  QLineEdit *keyInputEdit = nullptr;
  QLabel *keyInputHint = nullptr;

  QPushButton *onboardingNextButton = nullptr;
  QLabel *onboardingErrorLabel = nullptr;

  QString selectedSource; // "csfloat", "buff", "steam"
};

#endif // LOGINWINDOW_H
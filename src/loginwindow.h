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

  void showWelcomePage();
  void showQRPage();
  void showTokenPage();

  void setStatus(const QString &text, const QString &color = "gray");
  void handleLoginSuccess();

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
};

#endif // LOGINWINDOW_H
#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include "steamcompanion.h"

class LoginWindow : public QWidget
{
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
    QWidget     *welcomePage;
    QPushButton *qrLoginButton;
    QPushButton *tokenLoginButton;

    // ── QR page ───────────────────────────────
    QWidget     *qrPage;
    QLabel      *qrImageLabel;
    QLabel      *qrInstructionLabel;
    QLabel      *qrStatusLabel;
    QProgressBar *qrProgressBar;
    QPushButton *qrBackButton;

    // ── Token page ────────────────────────────
    QWidget     *tokenPage;
    QLabel      *tokenStatusLabel;
    QProgressBar *tokenProgressBar;
    QPushButton *tokenOpenBrowserButton;
    QLineEdit   *tokenPasteEdit;
    QPushButton *tokenSubmitButton;
    QPushButton *tokenBackButton;
};

#endif // LOGINWINDOW_H
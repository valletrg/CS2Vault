#ifndef QRLOGINDIALOG_H
#define QRLOGINDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QNetworkAccessManager>
#include <QNetworkReply>

// Shows a QR code image the user scans with their Steam mobile app.
// The companion emits qrCodeReady(url) with a challenge URL — we
// fetch the QR image from a QR generation service and display it.
class QRLoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QRLoginDialog(QWidget *parent = nullptr);

    void setQRUrl(const QString &challengeUrl);
    void setStatus(const QString &status);
    void markSuccess();

private:
    QLabel *qrImageLabel;
    QLabel *statusLabel;
    QLabel *instructionLabel;
    QPushButton *cancelButton;
    QNetworkAccessManager *nam;
};

#endif // QRLOGINDIALOG_H

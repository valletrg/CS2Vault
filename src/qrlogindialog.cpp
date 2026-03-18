#include "qrlogindialog.h"

#include <QFont>
#include <QHBoxLayout>
#include <QNetworkRequest>
#include <QPixmap>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

QRLoginDialog::QRLoginDialog(QWidget *parent)
    : QDialog(parent), nam(new QNetworkAccessManager(this)) {
  setWindowTitle("Sign in with Steam");
  setModal(true);
  setFixedSize(340, 420);

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setAlignment(Qt::AlignCenter);
  layout->setSpacing(12);

  QLabel *titleLabel = new QLabel("Sign in to Steam", this);
  QFont titleFont;
  titleFont.setPointSize(14);
  titleFont.setBold(true);
  titleLabel->setFont(titleFont);
  titleLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(titleLabel);

  instructionLabel = new QLabel("Open the Steam app on your phone,\n"
                                "tap the menu → Sign in via QR code,\n"
                                "then scan the code below.",
                                this);
  instructionLabel->setAlignment(Qt::AlignCenter);
  instructionLabel->setWordWrap(true);
  layout->addWidget(instructionLabel);

  qrImageLabel = new QLabel(this);
  qrImageLabel->setFixedSize(240, 240);
  qrImageLabel->setAlignment(Qt::AlignCenter);
  qrImageLabel->setStyleSheet("border: 1px solid #ccc; background: white;");
  qrImageLabel->setText("Loading QR code...");
  layout->addWidget(qrImageLabel, 0, Qt::AlignCenter);

  statusLabel = new QLabel("Waiting for scan...", this);
  statusLabel->setAlignment(Qt::AlignCenter);
  statusLabel->setStyleSheet("color: gray;");
  layout->addWidget(statusLabel);

  cancelButton = new QPushButton("Cancel", this);
  layout->addWidget(cancelButton);

  connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

void QRLoginDialog::setQRUrl(const QString &challengeUrl) {
  // Use the Google Charts QR API to render the challenge URL as an image.
  // This is a simple free service — no API key needed.
  // The URL we're encoding is Steam's own challenge URL, not sensitive.
  QUrl apiUrl("https://chart.googleapis.com/chart");
  QUrlQuery query;
  query.addQueryItem("chs", "240x240");
  query.addQueryItem("cht", "qr");
  query.addQueryItem("chl", challengeUrl);
  query.addQueryItem("choe", "UTF-8");
  apiUrl.setQuery(query);

  QNetworkRequest request(apiUrl);
  QNetworkReply *reply = nam->get(request);

  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      qrImageLabel->setText(
          "Could not load QR image.\nCheck your internet connection.");
      return;
    }
    QPixmap pixmap;
    pixmap.loadFromData(reply->readAll());
    if (!pixmap.isNull()) {
      qrImageLabel->setPixmap(pixmap.scaled(240, 240, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation));
    }
  });
}

void QRLoginDialog::setStatus(const QString &status) {
  statusLabel->setText(status);
}

void QRLoginDialog::markSuccess() {
  statusLabel->setText("✓ Logged in successfully!");
  statusLabel->setStyleSheet("color: green; font-weight: bold;");
  instructionLabel->setText("You are now signed in to Steam.");
  qrImageLabel->setText("✓");
  qrImageLabel->setStyleSheet(
      "border: 1px solid green; background: #f0fff0; font-size: 48px;");
  cancelButton->setText("Close");

  // Auto-close after 2 seconds
  QTimer::singleShot(2000, this, &QDialog::accept);
}

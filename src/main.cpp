#include "loginwindow.h"
#include "mainwindow.h"

#include <QApplication>
#include <QSharedPointer>
#include <QStyleFactory>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  QApplication::setApplicationName("CS2Vault");
  QApplication::setApplicationVersion("1.0.0");
  QApplication::setOrganizationName("CS2Vault");

  // ── Dark theme ────────────────────────────────────────────────────────────
  QApplication::setStyle(QStyleFactory::create("Fusion"));

  QPalette darkPalette;
  darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::WindowText, Qt::white);
  darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
  darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
  darkPalette.setColor(QPalette::ToolTipText, Qt::white);
  darkPalette.setColor(QPalette::Text, Qt::white);
  darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::ButtonText, Qt::white);
  darkPalette.setColor(QPalette::BrightText, Qt::red);
  darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::HighlightedText, Qt::black);
  QApplication::setPalette(darkPalette);

  // ── Login window ──────────────────────────────────────────────────────────
  LoginWindow *loginWindow = new LoginWindow();

  MainWindow *mainWindow = nullptr;

  QObject::connect(loginWindow, &LoginWindow::loginComplete,
                   [&loginWindow, &mainWindow]() {
                     // Transfer the authenticated companion to MainWindow
                     SteamCompanion *companion = loginWindow->takeCompanion();
                     mainWindow = new MainWindow(companion);
                     mainWindow->show();

                     loginWindow->close();
                     loginWindow->deleteLater();
                     loginWindow = nullptr;
                   });

  loginWindow->show();

  loginWindow->tryAutoLogin();

  return app.exec();
}
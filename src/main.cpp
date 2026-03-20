#include "loginwindow.h"
#include "mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QSharedPointer>
#include <QStyleFactory>
#include <QTextStream>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QFontDatabase::addApplicationFont(":/fonts/DMSans-Regular.ttf");
  QFontDatabase::addApplicationFont(":/fonts/DMSans-SemiBold.ttf");
  QFontDatabase::addApplicationFont(":/fonts/DMSans-Bold.ttf");
  QFontDatabase::addApplicationFont(":/fonts/JetBrainsMono-Regular.ttf");
  QFontDatabase::addApplicationFont(":/fonts/ZenDots-Regular.ttf");
  int dmId = QFontDatabase::addApplicationFont(":/fonts/DMSans-Regular.ttf");
  int id2 =
      QFontDatabase::addApplicationFont(":/fonts/JetBrainsMono-Regular.ttf");
  qDebug() << "DM Sans id:" << dmId
           << QFontDatabase::applicationFontFamilies(dmId);
  qDebug() << "JetBrains id:" << id2
           << QFontDatabase::applicationFontFamilies(id2);

  QApplication::setApplicationName("CS2Vault");
  QApplication::setApplicationVersion("1.0.0");
  QApplication::setOrganizationName("CS2Vault");

  // ── Dark theme ────────────────────────────────────────────────────────────
  QApplication::setStyle(QStyleFactory::create("Fusion"));

  QFile styleFile(":/style.qss");
  if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
    QString style = QTextStream(&styleFile).readAll();
    app.setStyleSheet(style);
  }

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
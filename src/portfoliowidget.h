#ifndef PORTFOLIOWIDGET_H
#define PORTFOLIOWIDGET_H

#include "steamapi.h"
#include "steamcompanion.h"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QQueue>
#include <QTableWidget>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

class QProgressDialog;
class PriceEmpireAPI;
class SteamAPI;
class PortfolioManager;

struct PriceCheckJob {
  QString portfolioId;
  int itemIndex;
  QString marketName;
};

class PortfolioWidget : public QWidget {
  Q_OBJECT

public:
  explicit PortfolioWidget(PriceEmpireAPI *api, SteamAPI *steamApi,
                        PortfolioManager *portfolioManager,
                        SteamCompanion *steamCompanion,
                        QWidget *parent = nullptr);
  ~PortfolioWidget();
  void updateAllPrices();

signals:
  void portfolioUpdated();

private slots:
  void onPortfolioSelected(int index);
  void onCreatePortfolio();
  void onDeletePortfolio();
  void onRenamePortfolio();
  void onAddItem();
  void onRemoveItem();
  void onEditItem();
  void onImportFromSteam();
  void onRefreshPrices();
  void onExportCSV();
  void onImportCSV();
  void onSteamInventoryFetched(const QVector<SteamInventoryItem> &items);
  void onSteamLoginSuccessful();
  void onSteamLoginFailed(const QString &error);
  void onPriceCheckTick();
  void onGCLoginClicked();
  void onGCImportClicked();
  void onGCStorageUnitClicked();
  void onGCInventoryReceived(const QList<GCItem> &items,
                             const QList<GCContainer> &containers);
  void onGCStorageUnitReceived(const QString &casketId,
                               const QList<GCItem> &items);

private:
  void setupUI();
  void refreshPortfolioList();
  void loadPortfolioItems();
  void populateTable();
  void calculateStatistics();
  void updateChart();
  void showImportDialog(const QVector<SteamInventoryItem> &items);
  void enqueuePriceCheck(const QString &portfolioId, int itemIndex,
                         const QString &marketName);
  void updateQueueStatusLabel();

  PriceEmpireAPI *api;
  SteamAPI *steamApi;
  PortfolioManager *portfolioManager;
  QComboBox *portfolioComboBox;
  QPushButton *createPortfolioButton;
  QPushButton *deletePortfolioButton;
  QPushButton *renamePortfolioButton;
  QTableWidget *portfolioTable;
  QPushButton *addButton;
  QPushButton *removeButton;
  QPushButton *editButton;
  QPushButton *refreshButton;
  QPushButton *exportButton;
  QPushButton *importButton;
  QLineEdit *steamIdEdit;
  QPushButton *steamLoginButton;
  QPushButton *importSteamButton;
  QLabel *steamStatusLabel;
  QLabel *totalInvestmentLabel;
  QLabel *currentValueLabel;
  QLabel *totalProfitLabel;
  QLabel *roiLabel;
  QTimer *autoSaveTimer;
  QString currentPortfolioId;
  QProgressDialog *loadingDialog;
  QWidget *queueGroup; // price-check status bar, hidden until active
QPushButton *gcShowImportDialogButton;
QList<GCItem> lastFetchedItems;

  // Steam companion
  SteamCompanion *steamCompanion;
  QPushButton *gcLoginButton;
  QPushButton *gcImportButton;
  QComboBox *gcStorageUnitCombo;
  QPushButton *gcStorageUnitButton;
  QLabel *gcStatusLabel;
  QList<GCContainer> gcContainers;

  QChart *portfolioChart;
  QChartView *chartView;
  QWidget *chartContainer;
  QPushButton *toggleChartButton;
  QLineSeries *valueSeries;
  QLineSeries *costSeries;
  QDateTimeAxis *axisX;
  QValueAxis *axisY;

  QQueue<PriceCheckJob> priceCheckQueue;
  QTimer *priceCheckTimer;
  int priceCheckTotal = 0;
  int priceCheckDone = 0;
  QLabel *queueStatusLabel;
  QPushButton *queuePauseButton;
  bool priceCheckPaused = false;
};

#endif // PORTFOLIOWIDGET_H

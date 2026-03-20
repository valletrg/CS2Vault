#ifndef PORTFOLIOWIDGET_H
#define PORTFOLIOWIDGET_H

#include "portfoliomanager.h"
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

  PriceEmpireAPI *api = nullptr;
  SteamAPI *steamApi = nullptr;
  PortfolioManager *portfolioManager = nullptr;

  QComboBox *portfolioComboBox = nullptr;
  QPushButton *createPortfolioButton = nullptr;
  QPushButton *deletePortfolioButton = nullptr;
  QPushButton *renamePortfolioButton = nullptr;
  QTableWidget *portfolioTable = nullptr;
  QPushButton *addButton = nullptr;
  QPushButton *removeButton = nullptr;
  QPushButton *editButton = nullptr;
  QPushButton *refreshButton = nullptr;
  QPushButton *exportButton = nullptr;
  QPushButton *importButton = nullptr;
  QString marketName(const PortfolioItem &item) const;

  // Hidden legacy widgets kept for slot compatibility
  QLineEdit *steamIdEdit = nullptr;
  QPushButton *steamLoginButton = nullptr;
  QPushButton *importSteamButton = nullptr;

  QLabel *steamStatusLabel = nullptr;
  QLabel *totalInvestmentLabel = nullptr;
  QLabel *currentValueLabel = nullptr;
  QLabel *totalProfitLabel = nullptr;
  QLabel *roiLabel = nullptr;

  QTimer *autoSaveTimer = nullptr;
  QString currentPortfolioId;
  QProgressDialog *loadingDialog = nullptr;

  // Price-check progress bar (shown during any fetch)
  QWidget *queueGroup = nullptr;
  QLabel *queueStatusLabel = nullptr;
  QPushButton *queuePauseButton = nullptr;

  QPushButton *gcShowImportDialogButton = nullptr;
  QList<GCItem> lastFetchedItems;

  // Steam companion / GC
  SteamCompanion *steamCompanion = nullptr;
  QPushButton *gcLoginButton = nullptr;
  QPushButton *gcImportButton = nullptr;
  QComboBox *gcStorageUnitCombo = nullptr;
  QPushButton *gcStorageUnitButton = nullptr;
  QLabel *gcStatusLabel = nullptr;
  QList<GCContainer> gcContainers;

  // Chart
  QChart *portfolioChart = nullptr;
  QChartView *chartView = nullptr;
  QWidget *chartContainer = nullptr;
  QPushButton *toggleChartButton = nullptr;
  QLineSeries *valueSeries = nullptr;
  QLineSeries *costSeries = nullptr;
  QDateTimeAxis *axisX = nullptr;
  QValueAxis *axisY = nullptr;

  // Steam throttled queue (used when source == Steam)
  QQueue<PriceCheckJob> priceCheckQueue;
  QTimer *priceCheckTimer = nullptr;
  int priceCheckTotal = 0;
  int priceCheckDone = 0;
  int priceCheckPending = 0;
  bool priceCheckPaused = false;
};

#endif // PORTFOLIOWIDGET_H
#ifndef WATCHLISTWIDGET_H
#define WATCHLISTWIDGET_H

#include "watchlistmanager.h"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

class PriceEmpireAPI;

class WatchlistWidget : public QWidget {
  Q_OBJECT

public:
  explicit WatchlistWidget(PriceEmpireAPI *api,
                           WatchlistManager *watchlistManager,
                           QWidget *parent = nullptr);
  ~WatchlistWidget();
  void updateAllPrices();

signals:
  void watchlistUpdated();

private slots:
  void onWatchlistSelected(int index);
  void onCreateWatchlist();
  void onDeleteWatchlist();
  void onRenameWatchlist();
  void onAddItem();
  void onRemoveItem();
  void onRefreshPrices();
  void onItemClicked(int row, int column);

private:
  void setupUI();
  void refreshWatchlistList();
  void loadWatchlistItems();
  void populateTable();
  void updateChart(int itemIndex);
  void updateStats();
  QString marketName(const WatchlistItem &item) const;
  QString calcChange(const WatchlistItem &item, qint64 secondsAgo) const;
  double calcChangeValue(const WatchlistItem &item, qint64 secondsAgo) const;
  void onTimeRangeChanged(const QString &range);

  PriceEmpireAPI *api = nullptr;
  WatchlistManager *watchlistManager = nullptr;

  // Watchlist selector
  QComboBox *watchlistComboBox = nullptr;
  QPushButton *createButton = nullptr;
  QPushButton *deleteButton = nullptr;
  QPushButton *renameButton = nullptr;

  // Stats
  QLabel *itemCountLabel = nullptr;
  QLabel *totalValueLabel = nullptr;
  QLabel *avgPriceLabel = nullptr;
  QLabel *topMoverLabel = nullptr;

  // Table
  QTableWidget *watchlistTable = nullptr;
  QPushButton *addButton = nullptr;
  QPushButton *removeButton = nullptr;
  QPushButton *refreshButton = nullptr;

  // Chart
  QChart *priceChart = nullptr;
  QChartView *chartView = nullptr;
  QLineSeries *priceSeries = nullptr;
  QDateTimeAxis *axisX = nullptr;
  QValueAxis *axisY = nullptr;
  QWidget *chartContainer = nullptr;
  QLabel *chartPlaceholder = nullptr;
  QLabel *chartItemLabel = nullptr;
  QLabel *priceBadge = nullptr;

  // Time range
  QList<QPushButton *> timeRangeButtons;
  QString selectedTimeRange = "All";

  QString currentWatchlistId;
  int selectedItemIndex = -1;
  QTimer *autoSaveTimer = nullptr;
};

#endif // WATCHLISTWIDGET_H

#ifndef TRADEHISTORYWIDGET_H
#define TRADEHISTORYWIDGET_H

#include "itemdatabase.h"
#include "tradehistory.h"
#include <QComboBox>
#include <QDateEdit>
#include <QLabel>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QWidget>

// Pill-shaped colored label delegate for the Type column
class TypePillDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override;
};

class TradeHistoryWidget : public QWidget {
  Q_OBJECT

public:
  explicit TradeHistoryWidget(TradeHistoryManager *manager,
                              ItemDatabase *itemDb,
                              QWidget *parent = nullptr);
  ~TradeHistoryWidget();

  static QString typeDisplayLabel(const QString &type);
  static QColor typeColor(const QString &type);

signals:
  void historyUpdated();

private slots:
  void onAddEntry();
  void onExportCSV();
  void onContextMenu(const QPoint &pos);
  void onDoubleClicked(int row, int column);
  void onFilterChanged();

private:
  void setupUI();
  void refreshTable();
  void updateSummary();

  TradeHistoryManager *m_manager = nullptr;
  ItemDatabase *m_itemDb = nullptr;

  QLabel *totalSpentLabel = nullptr;
  QLabel *totalReceivedLabel = nullptr;
  QLabel *netProfitLabel = nullptr;

  // Filter row
  QComboBox *filterTypeCombo = nullptr;
  QDateEdit *filterFromDate = nullptr;
  QDateEdit *filterToDate = nullptr;

  // Column indices
  static constexpr int COL_DATE = 0;
  static constexpr int COL_ITEM = 1;
  static constexpr int COL_TYPE = 2;
  static constexpr int COL_PRICE = 3;
  static constexpr int COL_QTY = 4;
  static constexpr int COL_TOTAL = 5;
  static constexpr int COL_PL = 6;
  static constexpr int COL_STORAGE = 7;
  static constexpr int COL_NOTES = 8;
  static constexpr int COL_COUNT = 9;

  QTableWidget *historyTable = nullptr;
  QPushButton *addButton = nullptr;
  QPushButton *exportButton = nullptr;
};

#endif // TRADEHISTORYWIDGET_H

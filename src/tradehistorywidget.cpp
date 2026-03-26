#include "tradehistorywidget.h"

#include <QApplication>
#include <QCompleter>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QSpinBox>
#include <QTextStream>
#include <QTimeZone>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// TypePillDelegate — renders a colored rounded pill in the Type column
// ---------------------------------------------------------------------------

void TypePillDelegate::paint(QPainter *painter,
                             const QStyleOptionViewItem &option,
                             const QModelIndex &index) const {
  // Draw selection background first
  if (option.state & QStyle::State_Selected) {
    painter->fillRect(option.rect, option.palette.highlight());
  }

  QString type = index.data(Qt::UserRole).toString();
  QString label = TradeHistoryWidget::typeDisplayLabel(type);
  QColor bg = TradeHistoryWidget::typeColor(type);

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);

  QFont font = option.font;
  int ptSize = font.pointSize();
  font.setPointSize(ptSize > 1 ? ptSize - 1 : 9);
  font.setBold(true);
  painter->setFont(font);

  QFontMetrics fm(font);
  int textW = fm.horizontalAdvance(label) + 12;
  int textH = fm.height() + 4;
  int x = option.rect.left() + (option.rect.width() - textW) / 2;
  int y = option.rect.top() + (option.rect.height() - textH) / 2;

  QRect pillRect(x, y, textW, textH);
  painter->setBrush(bg);
  painter->setPen(Qt::NoPen);
  painter->drawRoundedRect(pillRect, textH / 2.0, textH / 2.0);

  painter->setPen(QColor("#0f1117"));
  painter->drawText(pillRect, Qt::AlignCenter, label);

  painter->restore();
}

QSize TypePillDelegate::sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const {
  Q_UNUSED(index)
  return {100, qMax(option.rect.height(), 28)};
}

// ---------------------------------------------------------------------------
// Helper statics
// ---------------------------------------------------------------------------

QString TradeHistoryWidget::typeDisplayLabel(const QString &type) {
  if (type == "acquired")
    return "Acquired";
  if (type == "traded_away")
    return "Traded Away";
  if (type == "storage_in")
    return "Storage In";
  if (type == "storage_out")
    return "Storage Out";
  if (type == "manual_buy")
    return "Buy";
  if (type == "manual_sell")
    return "Sell";
  return type;
}

QColor TradeHistoryWidget::typeColor(const QString &type) {
  if (type == "acquired")
    return QColor("#81c784");
  if (type == "traded_away")
    return QColor("#e57373");
  if (type == "storage_in")
    return QColor("#4fc3f7");
  if (type == "storage_out")
    return QColor("#ffb74d");
  if (type == "manual_buy")
    return QColor("#64b5f6");
  if (type == "manual_sell")
    return QColor("#ce93d8");
  return QColor("#888");
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

TradeHistoryWidget::TradeHistoryWidget(TradeHistoryManager *manager,
                                       ItemDatabase *itemDb, QWidget *parent)
    : QWidget(parent), m_manager(manager), m_itemDb(itemDb) {
  setupUI();
  refreshTable();

  connect(m_manager, &TradeHistoryManager::historyChanged, this, [this]() {
    refreshTable();
    updateSummary();
    emit historyUpdated();
  });
}

TradeHistoryWidget::~TradeHistoryWidget() = default;

// ---------------------------------------------------------------------------
// setupUI
// ---------------------------------------------------------------------------

void TradeHistoryWidget::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(6);
  mainLayout->setContentsMargins(8, 8, 8, 8);

  // ── Summary bar ──────────────────────────────────────────────────────────
  auto *summaryBar = new QWidget(this);
  summaryBar->setStyleSheet(
      "QWidget { background: #1e2130; border: 1px solid #373a48; "
      "border-radius: 6px; }"
      "QLabel  { border: none; background: transparent; }");
  auto *summaryLayout = new QHBoxLayout(summaryBar);
  summaryLayout->setContentsMargins(12, 6, 12, 6);
  summaryLayout->setSpacing(24);

  auto makeStatPair = [&](const QString &title) -> QLabel * {
    auto *col = new QVBoxLayout();
    col->setSpacing(1);
    auto *titleLbl = new QLabel(title, summaryBar);
    titleLbl->setStyleSheet("color: #888; font-size: 10px;");
    auto *valueLbl =
        new QLabel(QString::fromUtf8("\xe2\x80\x94"), summaryBar);
    valueLbl->setStyleSheet(
        "color: #e0e0e0; font-size: 14px; font-weight: bold;");
    col->addWidget(titleLbl);
    col->addWidget(valueLbl);
    summaryLayout->addLayout(col);
    return valueLbl;
  };

  totalSpentLabel = makeStatPair("TOTAL SPENT");
  totalReceivedLabel = makeStatPair("TOTAL RECEIVED");
  netProfitLabel = makeStatPair("NET P/L");
  summaryLayout->addStretch();
  mainLayout->addWidget(summaryBar);

  // ── Filter row ───────────────────────────────────────────────────────────
  auto *filterBar = new QHBoxLayout();
  filterBar->setSpacing(8);

  auto *filterLabel = new QLabel("Filter:", this);
  filterLabel->setStyleSheet("color: #888; font-size: 11px;");
  filterBar->addWidget(filterLabel);

  filterTypeCombo = new QComboBox(this);
  filterTypeCombo->addItems({"All", "Acquired", "Traded Away", "Storage In",
                              "Storage Out", "Manual Buy", "Manual Sell"});
  filterTypeCombo->setFixedHeight(26);
  filterBar->addWidget(filterTypeCombo);

  auto *fromLabel = new QLabel("From:", this);
  fromLabel->setStyleSheet("color: #888; font-size: 11px;");
  filterBar->addWidget(fromLabel);

  filterFromDate = new QDateEdit(this);
  filterFromDate->setCalendarPopup(true);
  filterFromDate->setDate(QDate(2020, 1, 1));
  filterFromDate->setDisplayFormat("yyyy-MM-dd");
  filterFromDate->setFixedHeight(26);
  filterBar->addWidget(filterFromDate);

  auto *toLabel = new QLabel("To:", this);
  toLabel->setStyleSheet("color: #888; font-size: 11px;");
  filterBar->addWidget(toLabel);

  filterToDate = new QDateEdit(this);
  filterToDate->setCalendarPopup(true);
  filterToDate->setDate(QDate::currentDate());
  filterToDate->setDisplayFormat("yyyy-MM-dd");
  filterToDate->setFixedHeight(26);
  filterBar->addWidget(filterToDate);

  filterBar->addStretch();
  mainLayout->addLayout(filterBar);

  connect(filterTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &TradeHistoryWidget::onFilterChanged);
  connect(filterFromDate, &QDateEdit::dateChanged, this,
          &TradeHistoryWidget::onFilterChanged);
  connect(filterToDate, &QDateEdit::dateChanged, this,
          &TradeHistoryWidget::onFilterChanged);

  // ── Table ────────────────────────────────────────────────────────────────
  historyTable = new QTableWidget(this);
  historyTable->setColumnCount(COL_COUNT);
  historyTable->setHorizontalHeaderLabels({"Date", "Item", "Type", "Price",
                                            "Qty", "Total", "P/L",
                                            "Storage Unit", "Notes"});
  historyTable->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Interactive);
  historyTable->horizontalHeader()->setStretchLastSection(true);
  historyTable->horizontalHeader()->setMinimumSectionSize(50);
  historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  historyTable->setSortingEnabled(true);
  historyTable->setContextMenuPolicy(Qt::CustomContextMenu);

  // Type column pill delegate
  historyTable->setItemDelegateForColumn(COL_TYPE, new TypePillDelegate(historyTable));

  // Storage Unit column hidden by default
  historyTable->horizontalHeader()->setSectionHidden(COL_STORAGE, true);

  connect(historyTable, &QWidget::customContextMenuRequested, this,
          &TradeHistoryWidget::onContextMenu);
  connect(historyTable, &QTableWidget::cellDoubleClicked, this,
          &TradeHistoryWidget::onDoubleClicked);
  mainLayout->addWidget(historyTable, 1);

  // ── Buttons ──────────────────────────────────────────────────────────────
  auto *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(4);

  addButton = new QPushButton("Add Entry", this);
  addButton->setFixedHeight(28);
  exportButton = new QPushButton("Export CSV", this);
  exportButton->setFixedHeight(28);

  buttonLayout->addWidget(addButton);
  buttonLayout->addWidget(exportButton);
  buttonLayout->addStretch();
  mainLayout->addLayout(buttonLayout);

  connect(addButton, &QPushButton::clicked, this,
          &TradeHistoryWidget::onAddEntry);
  connect(exportButton, &QPushButton::clicked, this,
          &TradeHistoryWidget::onExportCSV);

  updateSummary();
}

// ---------------------------------------------------------------------------
// refreshTable
// ---------------------------------------------------------------------------

void TradeHistoryWidget::refreshTable() {
  auto entries = m_manager->entries();
  historyTable->setSortingEnabled(false);
  historyTable->setRowCount(entries.size());

  for (int i = 0; i < entries.size(); ++i) {
    const auto &e = entries[i];

    QDateTime dt =
        QDateTime::fromMSecsSinceEpoch(e.timestamp, QTimeZone::UTC);
    auto *dateItem =
        new QTableWidgetItem(dt.toLocalTime().toString("yyyy-MM-dd HH:mm"));
    dateItem->setData(Qt::UserRole, e.timestamp);
    dateItem->setData(Qt::UserRole + 1, e.id);

    auto *nameItem = new QTableWidgetItem(e.itemName);

    auto *typeItem = new QTableWidgetItem(typeDisplayLabel(e.type));
    typeItem->setData(Qt::UserRole, e.type);

    auto *priceItem =
        new QTableWidgetItem(QString("$%1").arg(e.price, 0, 'f', 2));
    priceItem->setData(Qt::UserRole, e.price);

    auto *qtyItem = new QTableWidgetItem(QString::number(e.quantity));

    double total = e.price * e.quantity;
    auto *totalItem =
        new QTableWidgetItem(QString("$%1").arg(total, 0, 'f', 2));
    totalItem->setData(Qt::UserRole, total);

    // P/L column
    auto *plItem = new QTableWidgetItem();
    if (e.buyPrice > 0.0 && e.sellPrice > 0.0) {
      double pl = (e.sellPrice - e.buyPrice) * e.quantity;
      plItem->setText(QString("$%1").arg(pl, 0, 'f', 2));
      plItem->setData(Qt::UserRole, pl);
      plItem->setForeground(pl >= 0 ? QColor("#28c878") : QColor("#dc4646"));
    }

    auto *storageItem = new QTableWidgetItem(e.storageUnit);
    auto *notesItem = new QTableWidgetItem(e.notes);

    historyTable->setItem(i, COL_DATE, dateItem);
    historyTable->setItem(i, COL_ITEM, nameItem);
    historyTable->setItem(i, COL_TYPE, typeItem);
    historyTable->setItem(i, COL_PRICE, priceItem);
    historyTable->setItem(i, COL_QTY, qtyItem);
    historyTable->setItem(i, COL_TOTAL, totalItem);
    historyTable->setItem(i, COL_PL, plItem);
    historyTable->setItem(i, COL_STORAGE, storageItem);
    historyTable->setItem(i, COL_NOTES, notesItem);
  }

  historyTable->setSortingEnabled(true);

  // Re-apply filters
  onFilterChanged();
}

// ---------------------------------------------------------------------------
// updateSummary
// ---------------------------------------------------------------------------

void TradeHistoryWidget::updateSummary() {
  double spent = m_manager->totalSpent();
  double received = m_manager->totalReceived();
  double net = m_manager->netProfit();

  totalSpentLabel->setText(QString("$%1").arg(spent, 0, 'f', 2));
  totalReceivedLabel->setText(QString("$%1").arg(received, 0, 'f', 2));
  netProfitLabel->setText(QString("$%1").arg(net, 0, 'f', 2));
  netProfitLabel->setStyleSheet(
      QString("font-size: 14px; font-weight: bold; color: %1;")
          .arg(net >= 0 ? "#28c878" : "#dc4646"));
}

// ---------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------

void TradeHistoryWidget::onFilterChanged() {
  QString filterText = filterTypeCombo->currentText();
  QDate fromDate = filterFromDate->date();
  QDate toDate = filterToDate->date();

  // Map combo text to internal type string
  static const QMap<QString, QString> typeMap = {
      {"Acquired", "acquired"},       {"Traded Away", "traded_away"},
      {"Storage In", "storage_in"},   {"Storage Out", "storage_out"},
      {"Manual Buy", "manual_buy"},   {"Manual Sell", "manual_sell"},
  };
  QString filterType = typeMap.value(filterText); // empty = "All"

  // Show/hide storage unit column based on filter
  bool showStorage =
      (filterType == "storage_in" || filterType == "storage_out");
  historyTable->horizontalHeader()->setSectionHidden(COL_STORAGE, !showStorage);

  for (int i = 0; i < historyTable->rowCount(); ++i) {
    bool visible = true;

    // Filter by type
    if (!filterType.isEmpty()) {
      auto *typeItem = historyTable->item(i, COL_TYPE);
      if (typeItem && typeItem->data(Qt::UserRole).toString() != filterType)
        visible = false;
    }

    // Filter by date range
    if (visible) {
      auto *dateItem = historyTable->item(i, COL_DATE);
      if (dateItem) {
        qint64 ts = dateItem->data(Qt::UserRole).toLongLong();
        QDate entryDate =
            QDateTime::fromMSecsSinceEpoch(ts, QTimeZone::UTC)
                .toLocalTime()
                .date();
        if (entryDate < fromDate || entryDate > toDate)
          visible = false;
      }
    }

    historyTable->setRowHidden(i, !visible);
  }
}

// ---------------------------------------------------------------------------
// Add entry dialog
// ---------------------------------------------------------------------------

void TradeHistoryWidget::onAddEntry() {
  QDialog dialog(this);
  dialog.setWindowTitle("Add Trade Entry");
  dialog.setMinimumWidth(450);
  auto *form = new QFormLayout(&dialog);

  // Item name with search-as-you-type
  auto *itemNameEdit = new QLineEdit(&dialog);
  itemNameEdit->setPlaceholderText("e.g. AK-47 | Redline (Field-Tested)");
  if (m_itemDb && m_itemDb->isLoaded()) {
    auto *completer = new QCompleter(m_itemDb->allItemNames(), &dialog);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setMaxVisibleItems(10);
    itemNameEdit->setCompleter(completer);
  }

  // Buy/Sell toggle
  auto *typeCombo = new QComboBox(&dialog);
  typeCombo->addItems({"Buy", "Sell"});

  // Price
  auto *priceSpinBox = new QDoubleSpinBox(&dialog);
  priceSpinBox->setRange(0.0, 999999.0);
  priceSpinBox->setDecimals(2);
  priceSpinBox->setPrefix("$");

  // Quantity
  auto *quantitySpinBox = new QSpinBox(&dialog);
  quantitySpinBox->setRange(1, 9999);
  quantitySpinBox->setValue(1);

  // Date picker
  auto *dateTimeEdit =
      new QDateTimeEdit(QDateTime::currentDateTime(), &dialog);
  dateTimeEdit->setCalendarPopup(true);
  dateTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm");

  // Notes
  auto *notesEdit = new QLineEdit(&dialog);
  notesEdit->setPlaceholderText("Optional notes");

  form->addRow("Item Name:", itemNameEdit);
  form->addRow("Type:", typeCombo);
  form->addRow("Price:", priceSpinBox);
  form->addRow("Quantity:", quantitySpinBox);
  form->addRow("Date:", dateTimeEdit);
  form->addRow("Notes:", notesEdit);

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted)
    return;

  QString itemName = itemNameEdit->text().trimmed();
  if (itemName.isEmpty()) {
    QMessageBox::warning(this, "Invalid", "Item name cannot be empty.");
    return;
  }

  TradeHistoryEntry entry;
  entry.itemName = itemName;
  entry.type = typeCombo->currentIndex() == 0 ? "manual_buy" : "manual_sell";
  entry.price = priceSpinBox->value();
  if (entry.type == "manual_buy")
    entry.buyPrice = priceSpinBox->value();
  else
    entry.sellPrice = priceSpinBox->value();
  entry.quantity = quantitySpinBox->value();
  entry.timestamp = dateTimeEdit->dateTime().toMSecsSinceEpoch();
  entry.notes = notesEdit->text();

  m_manager->addEntry(entry);
}

// ---------------------------------------------------------------------------
// Edit dialog on double-click
// ---------------------------------------------------------------------------

void TradeHistoryWidget::onDoubleClicked(int row, int /*column*/) {
  auto *dateItem = historyTable->item(row, COL_DATE);
  if (!dateItem)
    return;

  QString entryId = dateItem->data(Qt::UserRole + 1).toString();
  if (entryId.isEmpty())
    return;

  // Find the entry
  TradeHistoryEntry entry;
  bool found = false;
  for (const auto &e : m_manager->entries()) {
    if (e.id == entryId) {
      entry = e;
      found = true;
      break;
    }
  }
  if (!found)
    return;

  QDialog dialog(this);
  dialog.setWindowTitle("Edit Trade Entry");
  dialog.setMinimumWidth(400);
  auto *form = new QFormLayout(&dialog);

  auto *itemLabel = new QLabel(entry.itemName, &dialog);
  itemLabel->setStyleSheet("color: #e0e0e0;");
  form->addRow("Item:", itemLabel);

  auto *typeLabel = new QLabel(typeDisplayLabel(entry.type), &dialog);
  typeLabel->setStyleSheet(
      QString("color: %1; font-weight: bold;")
          .arg(typeColor(entry.type).name()));
  form->addRow("Type:", typeLabel);

  auto *buyPriceSpinBox = new QDoubleSpinBox(&dialog);
  buyPriceSpinBox->setRange(0.0, 999999.0);
  buyPriceSpinBox->setDecimals(2);
  buyPriceSpinBox->setPrefix("$");
  buyPriceSpinBox->setValue(entry.buyPrice);
  form->addRow("Buy Price:", buyPriceSpinBox);

  auto *sellPriceSpinBox = new QDoubleSpinBox(&dialog);
  sellPriceSpinBox->setRange(0.0, 999999.0);
  sellPriceSpinBox->setDecimals(2);
  sellPriceSpinBox->setPrefix("$");
  sellPriceSpinBox->setValue(entry.sellPrice);
  form->addRow("Sell Price:", sellPriceSpinBox);

  auto *notesEdit = new QLineEdit(entry.notes, &dialog);
  form->addRow("Notes:", notesEdit);

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted)
    return;

  entry.buyPrice = buyPriceSpinBox->value();
  entry.sellPrice = sellPriceSpinBox->value();
  entry.notes = notesEdit->text();

  m_manager->updateEntry(entry);
}

// ---------------------------------------------------------------------------
// Export CSV
// ---------------------------------------------------------------------------

void TradeHistoryWidget::onExportCSV() {
  auto entries = m_manager->entries();
  if (entries.isEmpty()) {
    QMessageBox::information(this, "Export", "No trade history to export.");
    return;
  }

  QString fileName = QFileDialog::getSaveFileName(
      this, "Export Trade History", "trade_history.csv", "CSV Files (*.csv)");
  if (fileName.isEmpty())
    return;

  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, "Error", "Could not open file for writing.");
    return;
  }

  QTextStream out(&file);
  out << "Date,Item,Type,Price,Quantity,Total,Buy Price,Sell Price,P/L,"
         "Storage Unit,Notes\n";

  for (const auto &e : entries) {
    QDateTime dt =
        QDateTime::fromMSecsSinceEpoch(e.timestamp, QTimeZone::UTC);
    double total = e.price * e.quantity;
    double pl = (e.buyPrice > 0.0 && e.sellPrice > 0.0)
                    ? (e.sellPrice - e.buyPrice) * e.quantity
                    : 0.0;
    out << QString("\"%1\",\"%2\",%3,%4,%5,%6,%7,%8,%9,\"%10\",\"%11\"\n")
               .arg(dt.toLocalTime().toString("yyyy-MM-dd HH:mm"))
               .arg(e.itemName)
               .arg(e.type)
               .arg(e.price)
               .arg(e.quantity)
               .arg(total)
               .arg(e.buyPrice)
               .arg(e.sellPrice)
               .arg(pl)
               .arg(e.storageUnit)
               .arg(e.notes);
  }

  file.close();
  QMessageBox::information(
      this, "Export Complete",
      QString("Exported %1 trade entries.").arg(entries.size()));
}

// ---------------------------------------------------------------------------
// Context menu (right-click delete)
// ---------------------------------------------------------------------------

void TradeHistoryWidget::onContextMenu(const QPoint &pos) {
  int row = historyTable->rowAt(pos.y());
  if (row < 0)
    return;

  auto *dateItem = historyTable->item(row, COL_DATE);
  if (!dateItem)
    return;

  QString entryId = dateItem->data(Qt::UserRole + 1).toString();
  if (entryId.isEmpty())
    return;

  QMenu menu(this);
  QAction *deleteAction = menu.addAction("Delete Entry");

  QAction *chosen = menu.exec(historyTable->viewport()->mapToGlobal(pos));
  if (chosen == deleteAction) {
    auto reply = QMessageBox::question(this, "Confirm Delete",
                                       "Delete this trade entry?",
                                       QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes)
      m_manager->removeEntry(entryId);
  }
}

#include "calculatorwidget.h"
#include "priceempireapi.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QSplitter>
#include <QFrame>

// Wear tier boundaries used across both calculators
static const QStringList CONDITIONS = {
    "Factory New", "Minimal Wear", "Field-Tested", "Well-Worn", "Battle-Scarred"
};

static const QStringList WEAPONS = {
    // Rifles
    "AK-47", "M4A4", "M4A1-S", "AWP", "FAMAS", "Galil AR", "AUG", "SG 553",
    "SSG 08", "G3SG1", "SCAR-20",
    // Pistols
    "Desert Eagle", "USP-S", "Glock-18", "P2000", "P250", "Five-SeveN",
    "CZ75-Auto", "Tec-9", "Dual Berettas", "R8 Revolver",
    // SMGs
    "MP9", "MAC-10", "PP-Bizon", "MP7", "UMP-45", "P90", "MP5-SD",
    // Heavy
    "Nova", "XM1014", "Sawed-Off", "MAG-7", "M249", "Negev",
    // Knives
    "★ Karambit", "★ M9 Bayonet", "★ Bayonet", "★ Butterfly Knife",
    "★ Falchion Knife", "★ Shadow Daggers", "★ Gut Knife", "★ Flip Knife",
    "★ Navaja Knife", "★ Stiletto Knife", "★ Talon Knife", "★ Ursus Knife",
    "★ Huntsman Knife", "★ Bowie Knife", "★ Skeleton Knife", "★ Kukri Knife",
    // Gloves
    "★ Sport Gloves", "★ Specialist Gloves", "★ Moto Gloves",
    "★ Bloodhound Gloves", "★ Hand Wraps", "★ Hydra Gloves",
    "★ Driver Gloves", "★ Broken Fang Gloves"
};

// ─────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────

QString CalculatorWidget::buildMarketName(const QString &weapon,
                                          const QString &skin,
                                          const QString &condition)
{
    // Produces e.g. "AK-47 | Redline (Field-Tested)"
    return QString("%1 | %2 (%3)").arg(weapon, skin.trimmed(), condition);
}

QString CalculatorWidget::formatPrice(double price)
{
    if (price <= 0.0) return "No listings";
    return QString("$%1").arg(price, 0, 'f', 2);
}

QString CalculatorWidget::wearFromFloat(double f)
{
    if (f < 0.07) return "Factory New";
    if (f < 0.15) return "Minimal Wear";
    if (f < 0.38) return "Field-Tested";
    if (f < 0.45) return "Well-Worn";
    return "Battle-Scarred";
}

// ─────────────────────────────────────────────
//  Constructor / Destructor
// ─────────────────────────────────────────────

CalculatorWidget::CalculatorWidget(PriceEmpireAPI *api, QWidget *parent)
    : QWidget(parent)
    , api(api)
{
    setupUI();
}

CalculatorWidget::~CalculatorWidget() = default;

// ─────────────────────────────────────────────
//  Top-level UI
// ─────────────────────────────────────────────

void CalculatorWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    calculatorTabs = new QTabWidget(this);

    setupPriceCheckerTab();
    setupTradeUpTab();

    mainLayout->addWidget(calculatorTabs);
}

// ─────────────────────────────────────────────
//  Tab 1 — Price Checker
// ─────────────────────────────────────────────

void CalculatorWidget::setupPriceCheckerTab()
{
    QWidget *widget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(widget);

    // ── Input group ──────────────────────────
    QGroupBox *inputGroup = new QGroupBox("Skin Details", widget);
    QGridLayout *inputGrid = new QGridLayout(inputGroup);

    pcWeaponCombo = new QComboBox(inputGroup);
    pcWeaponCombo->addItems(WEAPONS);

    pcSkinEdit = new QLineEdit(inputGroup);
    pcSkinEdit->setPlaceholderText("e.g. Redline, Asiimov, Fade...");

    pcConditionCombo = new QComboBox(inputGroup);
    pcConditionCombo->addItems(CONDITIONS);
    pcConditionCombo->setCurrentText("Field-Tested");

    pcBuyPriceSpinBox = new QDoubleSpinBox(inputGroup);
    pcBuyPriceSpinBox->setRange(0.0, 100000.0);
    pcBuyPriceSpinBox->setDecimals(2);
    pcBuyPriceSpinBox->setPrefix("$ ");
    pcBuyPriceSpinBox->setValue(0.0);
    pcBuyPriceSpinBox->setSpecialValueText("(enter buy price)");

    inputGrid->addWidget(new QLabel("Weapon:"),    0, 0);
    inputGrid->addWidget(pcWeaponCombo,             0, 1);
    inputGrid->addWidget(new QLabel("Skin Name:"), 1, 0);
    inputGrid->addWidget(pcSkinEdit,                1, 1);
    inputGrid->addWidget(new QLabel("Condition:"), 2, 0);
    inputGrid->addWidget(pcConditionCombo,          2, 1);
    inputGrid->addWidget(new QLabel("Your Buy Price:"), 3, 0);
    inputGrid->addWidget(pcBuyPriceSpinBox,             3, 1);
    inputGrid->setColumnStretch(1, 1);

    mainLayout->addWidget(inputGroup);

    // ── Action buttons ────────────────────────
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    pcLookupButton = new QPushButton("Look Up Price", widget);
    pcLookupButton->setFixedHeight(36);
    pcCompareAllButton = new QPushButton("Compare All Conditions", widget);
    pcCompareAllButton->setFixedHeight(36);

    buttonLayout->addWidget(pcLookupButton);
    buttonLayout->addWidget(pcCompareAllButton);
    mainLayout->addLayout(buttonLayout);

    pcStatusLabel = new QLabel("", widget);
    pcStatusLabel->setStyleSheet("color: gray; font-style: italic;");
    mainLayout->addWidget(pcStatusLabel);

    // ── Single-condition results ──────────────
    QGroupBox *resultsGroup = new QGroupBox("Price Check Result", widget);
    QFormLayout *resultsForm = new QFormLayout(resultsGroup);

    QFont bold;
    bold.setBold(true);
    bold.setPointSize(11);

    pcCurrentPriceLabel = new QLabel("—", resultsGroup);
    pcCurrentPriceLabel->setFont(bold);

    pcBreakEvenLabel = new QLabel("—", resultsGroup);
    pcBreakEvenLabel->setFont(bold);

    pcProfitIfSoldLabel = new QLabel("—", resultsGroup);
    pcProfitIfSoldLabel->setFont(bold);

    resultsForm->addRow("Current Market Price:", pcCurrentPriceLabel);
    resultsForm->addRow("Break-Even Sell Price (after 15% fee):", pcBreakEvenLabel);
    resultsForm->addRow("Profit if Sold Now:", pcProfitIfSoldLabel);

    mainLayout->addWidget(resultsGroup);

    // ── Compare-all-conditions table ─────────
    QGroupBox *compareGroup = new QGroupBox("All Conditions Comparison", widget);
    QVBoxLayout *compareLayout = new QVBoxLayout(compareGroup);

    pcConditionsTable = new QTableWidget(5, 4, compareGroup);
    pcConditionsTable->setHorizontalHeaderLabels({
        "Condition", "Market Price", "Break-Even", "Profit vs Your Buy"
    });
    pcConditionsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    pcConditionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pcConditionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    pcConditionsTable->setAlternatingRowColors(true);
    pcConditionsTable->verticalHeader()->setVisible(false);

    // Pre-fill condition names in column 0
    for (int i = 0; i < CONDITIONS.size(); ++i) {
        pcConditionsTable->setItem(i, 0, new QTableWidgetItem(CONDITIONS[i]));
        pcConditionsTable->setItem(i, 1, new QTableWidgetItem("—"));
        pcConditionsTable->setItem(i, 2, new QTableWidgetItem("—"));
        pcConditionsTable->setItem(i, 3, new QTableWidgetItem("—"));
    }

    compareLayout->addWidget(pcConditionsTable);
    mainLayout->addWidget(compareGroup);

    // ── Connections ───────────────────────────
    connect(pcLookupButton, &QPushButton::clicked, this, &CalculatorWidget::onLookupPrice);
    connect(pcCompareAllButton, &QPushButton::clicked, this, &CalculatorWidget::onCompareAllConditions);

    // Allow pressing Enter in the skin name field to trigger lookup
    connect(pcSkinEdit, &QLineEdit::returnPressed, this, &CalculatorWidget::onLookupPrice);

    calculatorTabs->addTab(widget, "Price Checker");
}

// ─────────────────────────────────────────────
//  Tab 2 — Trade-Up Calculator
// ─────────────────────────────────────────────

void CalculatorWidget::setupTradeUpTab()
{
    QWidget *widget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(widget);

    // ── Add skin controls ─────────────────────
    QGroupBox *addGroup = new QGroupBox("Add Input Skin (10 max)", widget);
    QGridLayout *addGrid = new QGridLayout(addGroup);

    tuWeaponCombo = new QComboBox(addGroup);
    tuWeaponCombo->addItems(WEAPONS);

    tuSkinEdit = new QLineEdit(addGroup);
    tuSkinEdit->setPlaceholderText("Skin name e.g. Redline");

    tuConditionCombo = new QComboBox(addGroup);
    tuConditionCombo->addItems(CONDITIONS);
    tuConditionCombo->setCurrentText("Field-Tested");

    tuFloatSpinBox = new QDoubleSpinBox(addGroup);
    tuFloatSpinBox->setRange(0.0, 1.0);
    tuFloatSpinBox->setDecimals(8);
    tuFloatSpinBox->setSingleStep(0.001);
    tuFloatSpinBox->setValue(0.25);

    tuPriceSpinBox = new QDoubleSpinBox(addGroup);
    tuPriceSpinBox->setRange(0.0, 100000.0);
    tuPriceSpinBox->setDecimals(2);
    tuPriceSpinBox->setPrefix("$ ");

    tuAddButton = new QPushButton("Add Skin", addGroup);
    tuAddButton->setFixedHeight(32);

    addGrid->addWidget(new QLabel("Weapon:"),    0, 0);
    addGrid->addWidget(tuWeaponCombo,             0, 1);
    addGrid->addWidget(new QLabel("Skin:"),      0, 2);
    addGrid->addWidget(tuSkinEdit,                0, 3);
    addGrid->addWidget(new QLabel("Condition:"), 1, 0);
    addGrid->addWidget(tuConditionCombo,          1, 1);
    addGrid->addWidget(new QLabel("Float:"),     1, 2);
    addGrid->addWidget(tuFloatSpinBox,            1, 3);
    addGrid->addWidget(new QLabel("Price:"),     2, 0);
    addGrid->addWidget(tuPriceSpinBox,            2, 1);
    addGrid->addWidget(tuAddButton,               2, 3);
    addGrid->setColumnStretch(1, 1);
    addGrid->setColumnStretch(3, 1);

    mainLayout->addWidget(addGroup);

    // ── Input skins table ─────────────────────
    QGroupBox *inputGroup = new QGroupBox("Input Skins", widget);
    QVBoxLayout *inputLayout = new QVBoxLayout(inputGroup);

    tuInputTable = new QTableWidget(0, 4, inputGroup);
    tuInputTable->setHorizontalHeaderLabels({"Skin", "Condition", "Float", "Price"});
    tuInputTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    tuInputTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tuInputTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    tuInputTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tuInputTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tuInputTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tuInputTable->verticalHeader()->setVisible(false);
    tuInputTable->setMaximumHeight(220);

    QHBoxLayout *tableButtons = new QHBoxLayout();
    tuRemoveButton = new QPushButton("Remove Selected", inputGroup);
    tuClearButton  = new QPushButton("Clear All", inputGroup);
    tableButtons->addWidget(tuRemoveButton);
    tableButtons->addWidget(tuClearButton);
    tableButtons->addStretch();

    inputLayout->addWidget(tuInputTable);
    inputLayout->addLayout(tableButtons);
    mainLayout->addWidget(inputGroup);

    // ── Output skin info ──────────────────────
    QGroupBox *outputGroup = new QGroupBox("Expected Output Skin", widget);
    QGridLayout *outputGrid = new QGridLayout(outputGroup);

    tuOutputWeaponCombo = new QComboBox(outputGroup);
    tuOutputWeaponCombo->addItems(WEAPONS);

    tuOutputSkinEdit = new QLineEdit(outputGroup);
    tuOutputSkinEdit->setPlaceholderText("Output skin name e.g. Asiimov");

    // The output float range depends on which skin/collection the output is.
    // The user enters it manually since we don't have a collection DB.
    tuOutputMinFloatSpinBox = new QDoubleSpinBox(outputGroup);
    tuOutputMinFloatSpinBox->setRange(0.0, 1.0);
    tuOutputMinFloatSpinBox->setDecimals(4);
    tuOutputMinFloatSpinBox->setValue(0.0);

    tuOutputMaxFloatSpinBox = new QDoubleSpinBox(outputGroup);
    tuOutputMaxFloatSpinBox->setRange(0.0, 1.0);
    tuOutputMaxFloatSpinBox->setDecimals(4);
    tuOutputMaxFloatSpinBox->setValue(1.0);

    outputGrid->addWidget(new QLabel("Weapon:"),         0, 0);
    outputGrid->addWidget(tuOutputWeaponCombo,            0, 1);
    outputGrid->addWidget(new QLabel("Skin Name:"),      0, 2);
    outputGrid->addWidget(tuOutputSkinEdit,               0, 3);
    outputGrid->addWidget(new QLabel("Output Float Min:"), 1, 0);
    outputGrid->addWidget(tuOutputMinFloatSpinBox,          1, 1);
    outputGrid->addWidget(new QLabel("Output Float Max:"), 1, 2);
    outputGrid->addWidget(tuOutputMaxFloatSpinBox,          1, 3);
    outputGrid->setColumnStretch(1, 1);
    outputGrid->setColumnStretch(3, 1);

    QLabel *floatHint = new QLabel(
        "Tip: find min/max float values for the output skin on csgofloat.com or buff163", outputGroup);
    floatHint->setStyleSheet("color: gray; font-size: 10px;");
    outputGrid->addWidget(floatHint, 2, 0, 1, 4);

    mainLayout->addWidget(outputGroup);

    // ── Calculate button ──────────────────────
    tuCalculateButton = new QPushButton("Calculate Trade-Up", widget);
    tuCalculateButton->setFixedHeight(40);
    mainLayout->addWidget(tuCalculateButton);

    tuStatusLabel = new QLabel("", widget);
    tuStatusLabel->setStyleSheet("color: gray; font-style: italic;");
    mainLayout->addWidget(tuStatusLabel);

    // ── Results ───────────────────────────────
    QGroupBox *resultsGroup = new QGroupBox("Trade-Up Results", widget);
    QGridLayout *resultsGrid = new QGridLayout(resultsGroup);

    QFont bold;
    bold.setBold(true);

    tuTotalCostLabel   = new QLabel("—", resultsGroup);
    tuOutputFloatLabel = new QLabel("—", resultsGroup);
    tuOutputWearLabel  = new QLabel("—", resultsGroup);
    tuOutputPriceLabel = new QLabel("—", resultsGroup);
    tuProfitLabel      = new QLabel("—", resultsGroup);
    tuRoiLabel         = new QLabel("—", resultsGroup);

    for (QLabel *l : {tuTotalCostLabel, tuOutputFloatLabel, tuOutputWearLabel,
                      tuOutputPriceLabel, tuProfitLabel, tuRoiLabel}) {
        l->setFont(bold);
    }

    resultsGrid->addWidget(new QLabel("Total Input Cost:"),      0, 0);
    resultsGrid->addWidget(tuTotalCostLabel,                     0, 1);
    resultsGrid->addWidget(new QLabel("Output Float (avg):"),    1, 0);
    resultsGrid->addWidget(tuOutputFloatLabel,                   1, 1);
    resultsGrid->addWidget(new QLabel("Output Wear:"),           2, 0);
    resultsGrid->addWidget(tuOutputWearLabel,                    2, 1);
    resultsGrid->addWidget(new QLabel("Output Market Price:"),   3, 0);
    resultsGrid->addWidget(tuOutputPriceLabel,                   3, 1);
    resultsGrid->addWidget(new QLabel("Profit (after 15% fee):"), 4, 0);
    resultsGrid->addWidget(tuProfitLabel,                         4, 1);
    resultsGrid->addWidget(new QLabel("ROI:"),                   5, 0);
    resultsGrid->addWidget(tuRoiLabel,                           5, 1);
    resultsGrid->setColumnStretch(1, 1);

    mainLayout->addWidget(resultsGroup);

    // ── Connections ───────────────────────────
    connect(tuAddButton,       &QPushButton::clicked, this, &CalculatorWidget::onAddTradeUpSkin);
    connect(tuRemoveButton,    &QPushButton::clicked, this, &CalculatorWidget::onRemoveTradeUpSkin);
    connect(tuClearButton,     &QPushButton::clicked, this, &CalculatorWidget::onClearTradeUp);
    connect(tuCalculateButton, &QPushButton::clicked, this, &CalculatorWidget::onCalculateTradeUp);

    calculatorTabs->addTab(widget, "Trade-Up Calculator");
}

// ─────────────────────────────────────────────
//  Price Checker — Slots
// ─────────────────────────────────────────────

void CalculatorWidget::onLookupPrice()
{
    QString skin = pcSkinEdit->text().trimmed();
    if (skin.isEmpty()) {
        QMessageBox::warning(this, "Missing Info", "Please enter a skin name.");
        return;
    }

    QString weapon    = pcWeaponCombo->currentText();
    QString condition = pcConditionCombo->currentText();
    QString marketName = buildMarketName(weapon, skin, condition);

    pcStatusLabel->setText(QString("Fetching price for %1...").arg(marketName));
    pcLookupButton->setEnabled(false);
    QApplication::processEvents();

    double price = api->fetchPrice(marketName);

    pcLookupButton->setEnabled(true);

    if (price <= 0.0) {
        pcStatusLabel->setText("No listings found for that skin and condition.");
        pcCurrentPriceLabel->setText("No listings");
        pcBreakEvenLabel->setText("—");
        pcProfitIfSoldLabel->setText("—");
        return;
    }

    pcStatusLabel->setText(QString("Last checked: %1").arg(marketName));
    pcCurrentPriceLabel->setText(formatPrice(price));

    // Steam takes 15% (13% Steam + 2% game developer).
    // Break-even = buyPrice / (1 - 0.15) — the price you need to list at
    // so that after Steam takes its cut you get back what you paid.
    double buyPrice   = pcBuyPriceSpinBox->value();
    double breakEven  = buyPrice > 0.0 ? buyPrice / 0.85 : price / 0.85;
    double netRevenue = price * 0.85;
    double profit     = buyPrice > 0.0 ? netRevenue - buyPrice : 0.0;

    pcBreakEvenLabel->setText(formatPrice(breakEven));

    if (buyPrice > 0.0) {
        QString sign = profit >= 0 ? "+" : "";
        pcProfitIfSoldLabel->setText(
            QString("%1%2 (%3%)")
            .arg(sign)
            .arg(formatPrice(profit))
            .arg((profit / buyPrice * 100.0), 0, 'f', 1));

        QPalette pal = pcProfitIfSoldLabel->palette();
        pal.setColor(QPalette::WindowText, profit >= 0 ? QColor(0, 150, 0) : QColor(200, 0, 0));
        pcProfitIfSoldLabel->setPalette(pal);
    } else {
        pcProfitIfSoldLabel->setText("(enter buy price above)");
    }
}

void CalculatorWidget::onCompareAllConditions()
{
    QString skin = pcSkinEdit->text().trimmed();
    if (skin.isEmpty()) {
        QMessageBox::warning(this, "Missing Info", "Please enter a skin name.");
        return;
    }

    QString weapon   = pcWeaponCombo->currentText();
    double  buyPrice = pcBuyPriceSpinBox->value();

    pcCompareAllButton->setEnabled(false);
    pcLookupButton->setEnabled(false);

    // Clear existing table data
    for (int i = 0; i < 5; ++i) {
        pcConditionsTable->item(i, 1)->setText("Fetching...");
        pcConditionsTable->item(i, 2)->setText("—");
        pcConditionsTable->item(i, 3)->setText("—");
    }

    // Fetch all 5 conditions one by one, respecting rate limit
    for (int i = 0; i < CONDITIONS.size(); ++i) {
        QString marketName = buildMarketName(weapon, skin, CONDITIONS[i]);
        pcStatusLabel->setText(QString("Fetching %1 of 5: %2...")
                               .arg(i + 1).arg(CONDITIONS[i]));
        QApplication::processEvents();

        double price = api->fetchPrice(marketName);

        pcConditionsTable->item(i, 1)->setText(formatPrice(price));

        if (price > 0.0) {
            double breakEven = buyPrice > 0.0 ? buyPrice / 0.85 : price / 0.85;
            double profit    = buyPrice > 0.0 ? (price * 0.85) - buyPrice : 0.0;

            pcConditionsTable->item(i, 2)->setText(formatPrice(breakEven));

            QString profitStr = buyPrice > 0.0
                ? QString("%1%2").arg(profit >= 0 ? "+" : "").arg(formatPrice(profit))
                : "—";
            pcConditionsTable->item(i, 3)->setText(profitStr);

            // Colour the profit cell
            QColor col = profit >= 0 ? QColor(0, 150, 0) : QColor(200, 0, 0);
            if (buyPrice > 0.0)
                pcConditionsTable->item(i, 3)->setForeground(col);
        } else {
            pcConditionsTable->item(i, 2)->setText("—");
            pcConditionsTable->item(i, 3)->setText("—");
        }

        QApplication::processEvents();
    }

    pcCompareAllButton->setEnabled(true);
    pcLookupButton->setEnabled(true);
    pcStatusLabel->setText("All conditions fetched.");
}

// ─────────────────────────────────────────────
//  Trade-Up Calculator — Slots
// ─────────────────────────────────────────────

void CalculatorWidget::onAddTradeUpSkin()
{
    if (tuInputTable->rowCount() >= 10) {
        QMessageBox::warning(this, "Limit Reached",
                             "A trade-up contract takes exactly 10 skins.");
        return;
    }

    QString skin = tuSkinEdit->text().trimmed();
    if (skin.isEmpty()) {
        QMessageBox::warning(this, "Missing Info", "Please enter a skin name.");
        return;
    }

    QString weapon    = tuWeaponCombo->currentText();
    QString condition = tuConditionCombo->currentText();
    double  floatVal  = tuFloatSpinBox->value();
    double  price     = tuPriceSpinBox->value();

    QString displayName = weapon + " | " + skin;

    int row = tuInputTable->rowCount();
    tuInputTable->insertRow(row);
    tuInputTable->setItem(row, 0, new QTableWidgetItem(displayName));
    tuInputTable->setItem(row, 1, new QTableWidgetItem(condition));
    tuInputTable->setItem(row, 2, new QTableWidgetItem(QString::number(floatVal, 'f', 6)));
    tuInputTable->setItem(row, 3, new QTableWidgetItem(
        price > 0 ? QString("$%1").arg(price, 0, 'f', 2) : "Unknown"));

    // Store raw float and price in UserRole for calculations
    tuInputTable->item(row, 2)->setData(Qt::UserRole, floatVal);
    tuInputTable->item(row, 3)->setData(Qt::UserRole, price);

    tuStatusLabel->setText(QString("%1/10 skins added.").arg(tuInputTable->rowCount()));

    // Clear the skin name field ready for the next entry
    tuSkinEdit->clear();
    tuSkinEdit->setFocus();
}

void CalculatorWidget::onRemoveTradeUpSkin()
{
    int row = tuInputTable->currentRow();
    if (row >= 0) {
        tuInputTable->removeRow(row);
        tuStatusLabel->setText(QString("%1/10 skins added.").arg(tuInputTable->rowCount()));
    }
}

void CalculatorWidget::onClearTradeUp()
{
    tuInputTable->setRowCount(0);

    // Reset result labels
    tuTotalCostLabel->setText("—");
    tuOutputFloatLabel->setText("—");
    tuOutputWearLabel->setText("—");
    tuOutputPriceLabel->setText("—");
    tuProfitLabel->setText("—");
    tuRoiLabel->setText("—");
    tuStatusLabel->setText("");
}

void CalculatorWidget::onCalculateTradeUp()
{
    int count = tuInputTable->rowCount();

    if (count == 0) {
        QMessageBox::warning(this, "No Skins", "Add some input skins first.");
        return;
    }
    if (count != 10) {
        // Warn but still calculate — useful for planning partial contracts
        tuStatusLabel->setText(
            QString("Note: trade-up needs 10 skins, you have %1. Showing partial estimate.").arg(count));
    }

    // ── Step 1: sum cost and average float ───
    double totalCost  = 0.0;
    double floatSum   = 0.0;
    int    priceCount = 0;

    for (int i = 0; i < count; ++i) {
        double f = tuInputTable->item(i, 2)->data(Qt::UserRole).toDouble();
        double p = tuInputTable->item(i, 3)->data(Qt::UserRole).toDouble();

        floatSum += f;
        if (p > 0.0) {
            totalCost += p;
            priceCount++;
        }
    }

    double avgFloat = floatSum / count;

    // ── Step 2: clamp output float to output skin's float range ──
    // In CS2, output float = avgInputFloat mapped onto [outputMin, outputMax]:
    // outputFloat = outputMin + avgFloat * (outputMax - outputMin)
    double outMin = tuOutputMinFloatSpinBox->value();
    double outMax = tuOutputMaxFloatSpinBox->value();

    if (outMax <= outMin) {
        QMessageBox::warning(this, "Invalid Float Range",
                             "Output float max must be greater than min.");
        return;
    }

    double outputFloat = outMin + avgFloat * (outMax - outMin);
    QString outputWear = wearFromFloat(outputFloat);

    tuTotalCostLabel->setText(
        priceCount == count
            ? QString("$%1").arg(totalCost, 0, 'f', 2)
            : QString("$%1 (incomplete — %2/%3 prices known)")
              .arg(totalCost, 0, 'f', 2).arg(priceCount).arg(count));

    tuOutputFloatLabel->setText(QString::number(outputFloat, 'f', 8));
    tuOutputWearLabel->setText(outputWear);

    // ── Step 3: fetch output skin price if provided ───────────────
    QString outputSkin   = tuOutputSkinEdit->text().trimmed();
    QString outputWeapon = tuOutputWeaponCombo->currentText();

    if (!outputSkin.isEmpty()) {
        QString marketName = buildMarketName(outputWeapon, outputSkin, outputWear);
        tuStatusLabel->setText(QString("Fetching output price for %1...").arg(marketName));
        tuCalculateButton->setEnabled(false);
        QApplication::processEvents();

        double outputPrice = api->fetchPrice(marketName);
        tuCalculateButton->setEnabled(true);

        if (outputPrice > 0.0) {
            tuOutputPriceLabel->setText(QString("$%1").arg(outputPrice, 0, 'f', 2));

            // Profit = output net revenue - total input cost
            // Steam takes 15% on the sale
            double netRevenue = outputPrice * 0.85;
            double profit     = netRevenue - totalCost;
            double roi        = totalCost > 0.0 ? (profit / totalCost) * 100.0 : 0.0;

            QString sign = profit >= 0 ? "+" : "";
            tuProfitLabel->setText(
                QString("%1$%2").arg(sign).arg(qAbs(profit), 0, 'f', 2));
            tuRoiLabel->setText(
                QString("%1%2%").arg(sign).arg(roi, 0, 'f', 1));

            QPalette pal = tuProfitLabel->palette();
            QColor col = profit >= 0 ? QColor(0, 150, 0) : QColor(200, 0, 0);
            pal.setColor(QPalette::WindowText, col);
            tuProfitLabel->setPalette(pal);
            tuRoiLabel->setPalette(pal);

            tuStatusLabel->setText("Calculation complete.");
        } else {
            tuOutputPriceLabel->setText("No listings found");
            tuProfitLabel->setText("—");
            tuRoiLabel->setText("—");
            tuStatusLabel->setText("Could not fetch output skin price.");
        }
    } else {
        tuOutputPriceLabel->setText("(no output skin entered)");
        tuProfitLabel->setText("—");
        tuRoiLabel->setText("—");
        tuStatusLabel->setText("Enter output skin name to calculate profit.");
    }
}
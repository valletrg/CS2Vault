#ifndef CALCULATORWIDGET_H
#define CALCULATORWIDGET_H

#include <QWidget>
#include <QTabWidget>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QTableWidget>
#include <QGroupBox>
#include <QVector>

class PriceEmpireAPI;

class CalculatorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CalculatorWidget(PriceEmpireAPI *api, QWidget *parent = nullptr);
    ~CalculatorWidget();

private slots:
    // Price checker
    void onLookupPrice();
    void onCompareAllConditions();

    // Trade-up
    void onAddTradeUpSkin();
    void onRemoveTradeUpSkin();
    void onCalculateTradeUp();
    void onClearTradeUp();

private:
    void setupUI();
    void setupPriceCheckerTab();
    void setupTradeUpTab();

    // Helpers
    QString buildMarketName(const QString &weapon, const QString &skin, const QString &condition);
    double fetchAndDisplay(const QString &marketName, QLabel *targetLabel);
    QString formatPrice(double price);
    QString wearFromFloat(double f);

    PriceEmpireAPI *api;
    QTabWidget *calculatorTabs;

    // --- Price Checker ---
    QComboBox *pcWeaponCombo;
    QLineEdit *pcSkinEdit;
    QComboBox *pcConditionCombo;
    QDoubleSpinBox *pcBuyPriceSpinBox;
    QPushButton *pcLookupButton;
    QPushButton *pcCompareAllButton;

    // Single lookup results
    QLabel *pcCurrentPriceLabel;
    QLabel *pcBreakEvenLabel;
    QLabel *pcProfitIfSoldLabel;

    // Compare-all-conditions table
    QTableWidget *pcConditionsTable;

    // Status
    QLabel *pcStatusLabel;

    // --- Trade-Up Calculator ---
    // Input skin entry controls
    QComboBox *tuWeaponCombo;
    QLineEdit *tuSkinEdit;
    QComboBox *tuConditionCombo;
    QDoubleSpinBox *tuFloatSpinBox;
    QDoubleSpinBox *tuPriceSpinBox;
    QPushButton *tuAddButton;
    QPushButton *tuRemoveButton;
    QPushButton *tuClearButton;

    // Input skins table (up to 10 rows)
    QTableWidget *tuInputTable;

    // Output skin info
    QLineEdit *tuOutputSkinEdit;   // user types expected output skin name
    QComboBox *tuOutputWeaponCombo;
    QDoubleSpinBox *tuOutputMinFloatSpinBox;
    QDoubleSpinBox *tuOutputMaxFloatSpinBox;

    QPushButton *tuCalculateButton;

    // Results
    QLabel *tuTotalCostLabel;
    QLabel *tuOutputFloatLabel;
    QLabel *tuOutputWearLabel;
    QLabel *tuOutputPriceLabel;
    QLabel *tuProfitLabel;
    QLabel *tuRoiLabel;
    QLabel *tuStatusLabel;
};

#endif // CALCULATORWIDGET_H
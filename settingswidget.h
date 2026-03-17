#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

#include <QWidget>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>

class PriceEmpireAPI;

class SettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsWidget(PriceEmpireAPI *api, QWidget *parent = nullptr);
    ~SettingsWidget();

signals:
    void settingsChanged();

private slots:
    void onSaveSettings();
    void onTestAPI();

private:
    void setupUI();
    void loadSettings();
    void saveSettings();

    PriceEmpireAPI *api;
    
    QLabel *apiStatusLabel;
    QPushButton *testAPIButton;
    
    QCheckBox *autoUpdateCheckBox;
    QSpinBox *updateIntervalSpinBox;
    QComboBox *currencyComboBox;
    
    QPushButton *saveButton;
};

#endif // SETTINGSWIDGET_H

#pragma once

#include <QDialog>
#include <QList>
#include <QTimer>

class AudioEngine;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QTabWidget;

class AudioDialog : public QDialog {
    Q_OBJECT
public:
    enum Tab { EqualizerTab, DecodersTab, OutputTab };

    explicit AudioDialog(AudioEngine *engine, QWidget *parent = nullptr);

    void showTab(Tab tab);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QWidget *buildEqualizerTab();
    QWidget *buildDecodersTab();
    QWidget *buildOutputTab();

    void syncSliders();
    void refreshPresets();
    void applyPreset(const QString &name);
    void savePresetAs();
    void refreshStatus();
    void refreshDevices();

    AudioEngine *m_engine;
    QTabWidget *m_tabs;

    QCheckBox *m_eqEnabled = nullptr;
    QComboBox *m_presets = nullptr;
    QPushButton *m_deletePreset = nullptr;
    QSlider *m_preamp = nullptr;
    QLabel *m_preampValue = nullptr;
    QList<QSlider *> m_bands;
    QList<QLabel *> m_bandValues;
    QCheckBox *m_nightMode = nullptr;

    QLabel *m_status = nullptr;
    QComboBox *m_device = nullptr;
    QComboBox *m_channels = nullptr;
    QCheckBox *m_exclusive = nullptr;
    QDoubleSpinBox *m_delay = nullptr;
    QTimer m_statusTimer;
};

#include "AudioDialog.h"

#include "AudioEngine.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

// Sliders count half-decibels so the EQ moves in 0.5 dB steps.
constexpr int StepsPerDb = 2;
constexpr int RangeDb = 12;

QString formatDb(double value)
{
    return value > 0 ? QStringLiteral("+%1").arg(value, 0, 'f', 1) : QString::number(value, 'f', 1);
}

QString formatFrequency(int hz)
{
    return hz >= 1000 ? QStringLiteral("%1k").arg(hz / 1000) : QString::number(hz);
}

QSlider *makeBandSlider(QWidget *parent)
{
    auto *slider = new QSlider(Qt::Vertical, parent);
    slider->setObjectName(QStringLiteral("Band"));
    slider->setRange(-RangeDb * StepsPerDb, RangeDb * StepsPerDb);
    slider->setSingleStep(1);
    slider->setPageStep(StepsPerDb * 3);
    slider->setMinimumHeight(170);
    slider->setToolTip(QObject::tr("Double-click to reset"));
    return slider;
}

QLabel *makeSmallLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignCenter);
    label->setObjectName(QStringLiteral("BandLabel"));
    return label;
}

QLabel *makeNote(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("Note"));
    label->setWordWrap(true);
    return label;
}

// "ac3_fixed" → "ac3_fixed  (fixed-point)", so the choices mean something.
QString decoderLabel(const QString &driver)
{
    QString hint;
    if (driver.endsWith(QLatin1String("_fixed")))
        hint = QObject::tr("fixed-point");
    else if (driver.endsWith(QLatin1String("float")))
        hint = QObject::tr("floating-point");
    else if (driver.endsWith(QLatin1String("_at")))
        hint = QObject::tr("Apple AudioToolbox");
    else if (driver.endsWith(QLatin1String("_mf")))
        hint = QObject::tr("Windows Media Foundation");
    else if (driver.startsWith(QLatin1String("lib")))
        hint = QObject::tr("external library");
    else
        hint = QObject::tr("FFmpeg built-in");
    return QStringLiteral("%1  (%2)").arg(driver, hint);
}

} // namespace

AudioDialog::AudioDialog(AudioEngine *engine, QWidget *parent)
    : QDialog(parent)
    , m_engine(engine)
    , m_tabs(new QTabWidget(this))
{
    setWindowTitle(tr("Audio"));
    setWindowFlag(Qt::Tool);
    setObjectName(QStringLiteral("AudioDialog"));

    m_tabs->addTab(buildEqualizerTab(), tr("Equalizer"));
    m_tabs->addTab(buildDecodersTab(), tr("Decoders"));
    m_tabs->addTab(buildOutputTab(), tr("Output"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->addWidget(m_tabs);

    m_statusTimer.setInterval(1000);
    connect(&m_statusTimer, &QTimer::timeout, this, &AudioDialog::refreshStatus);
    connect(m_engine, &AudioEngine::eqChanged, this, &AudioDialog::syncSliders);

    syncSliders();
    resize(640, 430);
}

void AudioDialog::showTab(Tab tab)
{
    m_tabs->setCurrentIndex(tab);
    show();
    raise();
    activateWindow();
}

QWidget *AudioDialog::buildEqualizerTab()
{
    auto *page = new QWidget(this);

    m_eqEnabled = new QCheckBox(tr("Enable equalizer"), page);
    m_presets = new QComboBox(page);
    m_presets->setMinimumWidth(150);
    auto *savePreset = new QPushButton(tr("Save as…"), page);
    m_deletePreset = new QPushButton(tr("Delete"), page);

    auto *top = new QHBoxLayout;
    top->addWidget(m_eqEnabled);
    top->addStretch(1);
    top->addWidget(new QLabel(tr("Preset"), page));
    top->addWidget(m_presets);
    top->addWidget(savePreset);
    top->addWidget(m_deletePreset);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);
    m_preamp = makeBandSlider(page);
    m_preampValue = makeSmallLabel(QString(), page);
    grid->addWidget(m_preampValue, 0, 0);
    grid->addWidget(m_preamp, 1, 0, Qt::AlignHCenter);
    grid->addWidget(makeSmallLabel(tr("Preamp"), page), 2, 0);
    grid->setColumnMinimumWidth(1, 18);
    for (int i = 0; i < AudioEngine::BandCount; ++i) {
        QSlider *slider = makeBandSlider(page);
        QLabel *value = makeSmallLabel(QString(), page);
        grid->addWidget(value, 0, i + 2);
        grid->addWidget(slider, 1, i + 2, Qt::AlignHCenter);
        grid->addWidget(makeSmallLabel(formatFrequency(AudioEngine::Frequencies[i]), page), 2, i + 2);
        m_bands << slider;
        m_bandValues << value;
        slider->installEventFilter(this);
        connect(slider, &QSlider::valueChanged, this, [this, i](int steps) {
            m_engine->setGain(i, double(steps) / StepsPerDb);
        });
    }
    m_preamp->installEventFilter(this);
    connect(m_preamp, &QSlider::valueChanged, this, [this](int steps) {
        m_engine->setPreamp(double(steps) / StepsPerDb);
    });

    auto *reset = new QPushButton(tr("Reset to flat"), page);
    m_nightMode = new QCheckBox(tr("Night mode: evens out loud action and quiet dialogue"), page);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_nightMode);
    bottom->addStretch(1);
    bottom->addWidget(reset);

    auto *layout = new QVBoxLayout(page);
    layout->addLayout(top);
    layout->addSpacing(6);
    layout->addLayout(grid, 1);
    layout->addLayout(bottom);
    layout->addWidget(makeNote(tr("Changes apply instantly. A limiter keeps boosted bands from clipping. "
                                  "Formats sent through passthrough (Decoders tab) bypass the equalizer."), page));

    m_eqEnabled->setChecked(m_engine->eqEnabled());
    m_nightMode->setChecked(m_engine->nightMode());
    connect(m_eqEnabled, &QCheckBox::toggled, m_engine, &AudioEngine::setEqEnabled);
    connect(m_nightMode, &QCheckBox::toggled, m_engine, &AudioEngine::setNightMode);
    connect(m_presets, &QComboBox::activated, this, [this](int index) { applyPreset(m_presets->itemText(index)); });
    connect(savePreset, &QPushButton::clicked, this, &AudioDialog::savePresetAs);
    connect(m_deletePreset, &QPushButton::clicked, this, [this] {
        m_engine->deletePreset(m_presets->currentText());
        refreshPresets();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        m_engine->setPreamp(0);
        applyPreset(QStringLiteral("Flat"));
    });
    return page;
}

QWidget *AudioDialog::buildDecodersTab()
{
    auto *page = new QWidget(this);
    m_status = new QLabel(page);
    m_status->setObjectName(QStringLiteral("AudioStatus"));

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(6);
    grid->addWidget(makeSmallLabel(tr("Format"), page), 0, 0, Qt::AlignLeft);
    grid->addWidget(makeSmallLabel(tr("Decoder"), page), 0, 1, Qt::AlignLeft);
    grid->addWidget(makeSmallLabel(tr("Passthrough"), page), 0, 2);

    int row = 1;
    for (const AudioCodecInfo &codec : AudioEngine::codecs()) {
        const QList<AudioDecoder> decoders = m_engine->decodersFor(codec.codec);
        if (decoders.isEmpty() && codec.passthrough.isEmpty())
            continue;
        grid->addWidget(new QLabel(codec.label, page), row, 0);

        if (decoders.isEmpty()) {
            grid->addWidget(makeNote(tr("Decoded as DTS core"), page), row, 1);
        } else {
            auto *combo = new QComboBox(page);
            combo->addItem(tr("Automatic"), QString());
            for (const AudioDecoder &decoder : decoders) {
                combo->addItem(decoderLabel(decoder.driver), decoder.driver);
                combo->setItemData(combo->count() - 1, decoder.description, Qt::ToolTipRole);
            }
            const int current = combo->findData(m_engine->decoderFor(codec.codec));
            combo->setCurrentIndex(current < 0 ? 0 : current);
            combo->setEnabled(decoders.size() > 1);
            const QString name = codec.codec;
            connect(combo, &QComboBox::activated, this, [this, combo, name] {
                m_engine->setDecoderFor(name, combo->currentData().toString());
                QTimer::singleShot(600, this, &AudioDialog::refreshStatus);
            });
            grid->addWidget(combo, row, 1);
        }

        if (!codec.passthrough.isEmpty()) {
            auto *box = new QCheckBox(page);
            box->setChecked(m_engine->passthrough(codec.passthrough));
            const QString format = codec.passthrough;
            connect(box, &QCheckBox::toggled, this, [this, format](bool on) {
                m_engine->setPassthrough(format, on);
                QTimer::singleShot(600, this, &AudioDialog::refreshStatus);
            });
            grid->addWidget(box, row, 2, Qt::AlignHCenter);
        }
        ++row;
    }
    grid->setColumnStretch(1, 1);

    auto *layout = new QVBoxLayout(page);
    layout->addWidget(m_status);
    layout->addSpacing(6);
    layout->addLayout(grid);
    layout->addStretch(1);
    layout->addWidget(makeNote(tr("Passthrough sends the original Dolby/DTS stream untouched to an AV receiver or "
                                  "soundbar over HDMI or S/PDIF. Only tick formats your receiver supports; with "
                                  "laptop speakers or headphones leave them off."), page));
    return page;
}

QWidget *AudioDialog::buildOutputTab()
{
    auto *page = new QWidget(this);
    m_device = new QComboBox(page);
    m_channels = new QComboBox(page);
    m_channels->addItem(tr("Automatic (match the device)"), QStringLiteral("auto-safe"));
    m_channels->addItem(tr("Stereo (downmix surround)"), QStringLiteral("stereo"));
    m_channels->addItem(tr("5.1 surround"), QStringLiteral("5.1"));
    m_channels->addItem(tr("7.1 surround"), QStringLiteral("7.1"));
    m_channels->setCurrentIndex(std::max(0, m_channels->findData(m_engine->channels())));
    m_exclusive = new QCheckBox(tr("Exclusive mode: bypass the system mixer (bit-perfect, other apps go silent)"), page);
    m_exclusive->setChecked(m_engine->exclusive());

    m_delay = new QDoubleSpinBox(page);
    m_delay->setRange(-10, 10);
    m_delay->setSingleStep(0.05);
    m_delay->setDecimals(2);
    m_delay->setSuffix(tr(" s"));
    auto *resetDelay = new QPushButton(tr("Reset"), page);
    auto *delayRow = new QHBoxLayout;
    delayRow->addWidget(m_delay);
    delayRow->addWidget(resetDelay);
    delayRow->addStretch(1);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setVerticalSpacing(10);
    form->addRow(tr("Device"), m_device);
    form->addRow(tr("Channels"), m_channels);
    form->addRow(QString(), m_exclusive);
    form->addRow(tr("Audio delay"), delayRow);

    auto *layout = new QVBoxLayout(page);
    layout->addLayout(form);
    layout->addStretch(1);
    layout->addWidget(makeNote(tr("Audio delay is for the current video only (keys [ and ]). Positive values "
                                  "play the sound later, for when voices come before lips move."), page));

    connect(m_device, &QComboBox::activated, this, [this] { m_engine->setDevice(m_device->currentData().toString()); });
    connect(m_channels, &QComboBox::activated, this, [this] { m_engine->setChannels(m_channels->currentData().toString()); });
    connect(m_exclusive, &QCheckBox::toggled, m_engine, &AudioEngine::setExclusive);
    connect(m_delay, &QDoubleSpinBox::valueChanged, m_engine, &AudioEngine::setDelay);
    connect(resetDelay, &QPushButton::clicked, this, [this] { m_delay->setValue(0); });
    return page;
}

void AudioDialog::syncSliders()
{
    const QSignalBlocker preampBlocker(m_preamp);
    m_preamp->setValue(int(std::lround(m_engine->preamp() * StepsPerDb)));
    m_preampValue->setText(formatDb(m_engine->preamp()));
    for (int i = 0; i < AudioEngine::BandCount; ++i) {
        const QSignalBlocker blocker(m_bands[i]);
        const double gain = m_engine->gains()[i];
        m_bands[i]->setValue(int(std::lround(gain * StepsPerDb)));
        m_bandValues[i]->setText(formatDb(gain));
    }
    {
        const QSignalBlocker blocker(m_eqEnabled);
        m_eqEnabled->setChecked(m_engine->eqEnabled());
    }
    refreshPresets();
}

void AudioDialog::refreshPresets()
{
    const QSignalBlocker blocker(m_presets);
    m_presets->clear();
    const QString match = m_engine->matchingPreset();
    if (match.isEmpty())
        m_presets->addItem(tr("Custom"));
    m_presets->addItems(m_engine->presetNames());
    m_presets->setCurrentText(match.isEmpty() ? tr("Custom") : match);
    m_deletePreset->setEnabled(!match.isEmpty() && !m_engine->isBuiltInPreset(match));
}

void AudioDialog::applyPreset(const QString &name)
{
    if (name == tr("Custom"))
        return;
    m_engine->setGains(m_engine->preset(name));
    if (!m_engine->eqEnabled())
        m_engine->setEqEnabled(true);
}

void AudioDialog::savePresetAs()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Preset name"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (m_engine->isBuiltInPreset(name)) {
        QMessageBox::information(this, tr("Save preset"), tr("“%1” is a built-in preset. Pick another name.").arg(name));
        return;
    }
    m_engine->savePreset(name);
    refreshPresets();
}

void AudioDialog::refreshStatus()
{
    m_status->setText(m_engine->status());
}

void AudioDialog::refreshDevices()
{
    const QSignalBlocker blocker(m_device);
    m_device->clear();
    for (const AudioDevice &device : m_engine->devices())
        m_device->addItem(device.name == QLatin1String("auto") ? tr("System default") : device.description, device.name);
    m_device->setCurrentIndex(std::max(0, m_device->findData(m_engine->device())));
}

bool AudioDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (auto *slider = qobject_cast<QSlider *>(watched)) {
            slider->setValue(0);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void AudioDialog::showEvent(QShowEvent *event)
{
    refreshStatus();
    refreshDevices();
    {
        const QSignalBlocker blocker(m_delay);
        m_delay->setValue(m_engine->delay());
    }
    m_statusTimer.start();
    QDialog::showEvent(event);
}

void AudioDialog::hideEvent(QHideEvent *event)
{
    m_statusTimer.stop();
    m_engine->syncFilters();
    QDialog::hideEvent(event);
}

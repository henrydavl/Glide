#include "AudioEngine.h"

#include "MpvPlayer.h"

#include <QSettings>

#include <algorithm>
#include <cmath>

const std::array<int, AudioEngine::BandCount> AudioEngine::Frequencies = {
    31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000,
};

namespace {

// The mpv filter label used to address the chain with af-command.
const QString FilterLabel = QStringLiteral("glide");

struct BuiltInPreset {
    const char *name;
    AudioEngine::Gains gains;
};

const BuiltInPreset BuiltInPresets[] = {
    {"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"Bass boost", {6, 5, 4, 2, 0, 0, 0, 0, 0, 0}},
    {"Treble boost", {0, 0, 0, 0, 0, 0, 2, 4, 5, 6}},
    {"Dialogue", {-4, -3, -2, 0, 2, 4, 4, 3, 1, 0}},
    {"Loudness", {5, 4, 2, 0, -1, -1, 0, 2, 4, 5}},
    {"Rock", {4, 3, 2, -1, -2, -1, 1, 3, 4, 4}},
    {"Pop", {-1, 1, 3, 4, 3, 0, -1, -1, 1, 2}},
    {"Classical", {3, 2, 1, 0, 0, 0, -1, -1, 1, 2}},
    {"Electronic", {5, 4, 1, 0, -2, 1, 0, 1, 4, 5}},
};

QString dB(double value)
{
    return QString::number(value, 'f', 1) + QStringLiteral("dB");
}

QString bandTarget(int band)
{
    return QStringLiteral("equalizer@b%1").arg(band);
}

QVariantList toList(const AudioEngine::Gains &gains)
{
    QVariantList list;
    for (double gain : gains)
        list << gain;
    return list;
}

AudioEngine::Gains fromList(const QVariantList &list)
{
    AudioEngine::Gains gains{};
    for (int i = 0; i < AudioEngine::BandCount && i < list.size(); ++i)
        gains[i] = list[i].toDouble();
    return gains;
}

} // namespace

AudioEngine::AudioEngine(MpvPlayer *player, QObject *parent)
    : QObject(parent)
    , m_player(player)
{
    const QSettings settings;
    m_eqEnabled = settings.value(QStringLiteral("audio/eqEnabled"), false).toBool();
    m_preamp = settings.value(QStringLiteral("audio/preamp"), 0.0).toDouble();
    m_gains = fromList(settings.value(QStringLiteral("audio/gains")).toList());
    m_nightMode = settings.value(QStringLiteral("audio/nightMode"), false).toBool();
    m_decoders = settings.value(QStringLiteral("audio/decoders")).toMap();
    m_passthrough = settings.value(QStringLiteral("audio/passthrough")).toStringList();
    m_device = settings.value(QStringLiteral("audio/device"), m_device).toString();
    m_channels = settings.value(QStringLiteral("audio/channels"), m_channels).toString();
    m_exclusive = settings.value(QStringLiteral("audio/exclusive"), false).toBool();
}

void AudioEngine::configure()
{
    m_player->setOption("af", filterString());
    m_player->setOption("ad", decoderPriority());
    m_player->setOption("audio-spdif", m_passthrough.join(','));
    m_player->setOption("audio-device", m_device);
    m_player->setOption("audio-channels", m_channels);
    m_player->setOption("audio-exclusive", m_exclusive ? QStringLiteral("yes") : QStringLiteral("no"));
}

QString AudioEngine::filterString() const
{
    QStringList chain;
    if (m_eqEnabled) {
        chain << QStringLiteral("volume@pre=volume=%1").arg(dB(m_preamp));
        // Peaking filters one octave wide, centred on the ISO graphic-EQ frequencies.
        for (int i = 0; i < BandCount; ++i)
            chain << QStringLiteral("%1=f=%2:t=o:w=1:g=%3").arg(bandTarget(i)).arg(Frequencies[i]).arg(m_gains[i], 0, 'f', 1);
    }
    if (m_nightMode) {
        // A compressor rather than dynaudnorm/loudnorm: those look seconds ahead, which
        // would knock audio out of sync with the picture.
        chain << QStringLiteral("acompressor@night=threshold=0.1:ratio=4:attack=10:release=250:makeup=2.5");
    }
    if (chain.isEmpty())
        return {};
    // Boosted bands or makeup gain can push peaks past full scale; catch them before
    // they clip.
    chain << QStringLiteral("alimiter@limit=limit=0.97:level=disabled");
    return QStringLiteral("@%1:lavfi=[%2]").arg(FilterLabel, chain.join(','));
}

void AudioEngine::applyFilters()
{
    m_player->setPropertyString("af", filterString());
    m_filtersDirty = false;
}

void AudioEngine::syncFilters()
{
    if (m_filtersDirty)
        applyFilters();
}

bool AudioEngine::sendToGraph(const QString &command, const QString &value, const QString &target)
{
    // Fails while no audio is playing (there's no graph yet); the caller then rebuilds the
    // chain instead, which is seamless when idle.
    return m_player->commandSync({QStringLiteral("af-command"), FilterLabel, command, value, target});
}

void AudioEngine::setEqEnabled(bool enabled)
{
    if (enabled == m_eqEnabled)
        return;
    m_eqEnabled = enabled;
    applyFilters();
    save();
    emit eqChanged();
}

void AudioEngine::setPreamp(double value)
{
    m_preamp = std::clamp(value, -12.0, 12.0);
    if (m_eqEnabled) {
        if (sendToGraph(QStringLiteral("volume"), dB(m_preamp), QStringLiteral("volume@pre")))
            m_filtersDirty = true;
        else
            applyFilters();
    }
    save();
    emit eqChanged();
}

void AudioEngine::setGain(int band, double value)
{
    if (band < 0 || band >= BandCount)
        return;
    m_gains[band] = std::clamp(value, -12.0, 12.0);
    if (m_eqEnabled) {
        if (sendToGraph(QStringLiteral("g"), QString::number(m_gains[band], 'f', 1), bandTarget(band)))
            m_filtersDirty = true;
        else
            applyFilters();
    }
    save();
    emit eqChanged();
}

void AudioEngine::setGains(const Gains &gains)
{
    m_gains = gains;
    if (m_eqEnabled) {
        bool live = true;
        for (int i = 0; i < BandCount && live; ++i)
            live = sendToGraph(QStringLiteral("g"), QString::number(m_gains[i], 'f', 1), bandTarget(i));
        if (live)
            m_filtersDirty = true;
        else
            applyFilters();
    }
    save();
    emit eqChanged();
}

void AudioEngine::setNightMode(bool enabled)
{
    if (enabled == m_nightMode)
        return;
    m_nightMode = enabled;
    applyFilters();
    save();
}

QStringList AudioEngine::presetNames() const
{
    QStringList names;
    for (const BuiltInPreset &preset : BuiltInPresets)
        names << QString::fromLatin1(preset.name);
    const QStringList custom = QSettings().value(QStringLiteral("audio/presets")).toMap().keys();
    for (const QString &name : custom) {
        if (!names.contains(name))
            names << name;
    }
    return names;
}

bool AudioEngine::isBuiltInPreset(const QString &name) const
{
    return std::any_of(std::begin(BuiltInPresets), std::end(BuiltInPresets),
                       [&](const BuiltInPreset &preset) { return name == QLatin1String(preset.name); });
}

AudioEngine::Gains AudioEngine::preset(const QString &name) const
{
    for (const BuiltInPreset &preset : BuiltInPresets) {
        if (name == QLatin1String(preset.name))
            return preset.gains;
    }
    return fromList(QSettings().value(QStringLiteral("audio/presets")).toMap().value(name).toList());
}

void AudioEngine::savePreset(const QString &name)
{
    if (name.isEmpty() || isBuiltInPreset(name))
        return;
    QSettings settings;
    QVariantMap presets = settings.value(QStringLiteral("audio/presets")).toMap();
    presets.insert(name, toList(m_gains));
    settings.setValue(QStringLiteral("audio/presets"), presets);
}

void AudioEngine::deletePreset(const QString &name)
{
    QSettings settings;
    QVariantMap presets = settings.value(QStringLiteral("audio/presets")).toMap();
    presets.remove(name);
    settings.setValue(QStringLiteral("audio/presets"), presets);
}

QString AudioEngine::matchingPreset() const
{
    for (const QString &name : presetNames()) {
        const Gains gains = preset(name);
        bool same = true;
        for (int i = 0; i < BandCount && same; ++i)
            same = std::abs(gains[i] - m_gains[i]) < 0.05;
        if (same)
            return name;
    }
    return {};
}

const QList<AudioCodecInfo> &AudioEngine::codecs()
{
    static const QList<AudioCodecInfo> list = {
        {QStringLiteral("aac"), QStringLiteral("AAC"), {}},
        {QStringLiteral("ac3"), QStringLiteral("AC-3 (Dolby Digital)"), QStringLiteral("ac3")},
        {QStringLiteral("eac3"), QStringLiteral("E-AC-3 (Dolby Digital Plus)"), QStringLiteral("eac3")},
        {QStringLiteral("dts"), QStringLiteral("DTS"), QStringLiteral("dts")},
        {QStringLiteral("dts-hd"), QStringLiteral("DTS-HD (passthrough only)"), QStringLiteral("dts-hd")},
        {QStringLiteral("truehd"), QStringLiteral("Dolby TrueHD"), QStringLiteral("truehd")},
        {QStringLiteral("mp2"), QStringLiteral("MPEG Layer II (DVB / DVD)"), {}},
        {QStringLiteral("mp3"), QStringLiteral("MP3"), {}},
        {QStringLiteral("opus"), QStringLiteral("Opus"), {}},
        {QStringLiteral("flac"), QStringLiteral("FLAC"), {}},
        {QStringLiteral("vorbis"), QStringLiteral("Vorbis"), {}},
    };
    return list;
}

QList<AudioDecoder> AudioEngine::decodersFor(const QString &codec) const
{
    QList<AudioDecoder> decoders;
    const QVariantList list = m_player->propertyValue("decoder-list").toList();
    for (const QVariant &entry : list) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("codec")).toString() == codec)
            decoders.append({map.value(QStringLiteral("driver")).toString(), map.value(QStringLiteral("description")).toString()});
    }
    return decoders;
}

QString AudioEngine::decoderFor(const QString &codec) const
{
    return m_decoders.value(codec).toString();
}

void AudioEngine::setDecoderFor(const QString &codec, const QString &driver)
{
    if (driver.isEmpty())
        m_decoders.remove(codec);
    else
        m_decoders.insert(codec, driver);
    m_player->setPropertyString("ad", decoderPriority());
    save();
    reinitAudio();
}

QString AudioEngine::decoderPriority() const
{
    // mpv tries these first and falls back to its normal choice for every other codec.
    QStringList drivers;
    for (const QVariant &driver : m_decoders)
        drivers << driver.toString();
    return drivers.join(',');
}

bool AudioEngine::passthrough(const QString &format) const
{
    return m_passthrough.contains(format);
}

void AudioEngine::setPassthrough(const QString &format, bool enabled)
{
    if (enabled == m_passthrough.contains(format))
        return;
    if (enabled)
        m_passthrough << format;
    else
        m_passthrough.removeAll(format);
    m_player->setPropertyString("audio-spdif", m_passthrough.join(','));
    save();
    reinitAudio();
}

QList<AudioDevice> AudioEngine::devices() const
{
    QList<AudioDevice> devices;
    const QVariantList list = m_player->propertyValue("audio-device-list").toList();
    for (const QVariant &entry : list) {
        const QVariantMap map = entry.toMap();
        devices.append({map.value(QStringLiteral("name")).toString(), map.value(QStringLiteral("description")).toString()});
    }
    return devices;
}

void AudioEngine::setDevice(const QString &name)
{
    m_device = name.isEmpty() ? QStringLiteral("auto") : name;
    m_player->setPropertyString("audio-device", m_device);
    save();
}

void AudioEngine::setChannels(const QString &layout)
{
    m_channels = layout;
    m_player->setPropertyString("audio-channels", m_channels);
    save();
    m_player->command({QStringLiteral("ao-reload")});
}

void AudioEngine::setExclusive(bool exclusive)
{
    m_exclusive = exclusive;
    m_player->setPropertyString("audio-exclusive", exclusive ? QStringLiteral("yes") : QStringLiteral("no"));
    save();
    m_player->command({QStringLiteral("ao-reload")});
}

double AudioEngine::delay() const
{
    return m_player->propertyString("audio-delay").toDouble();
}

void AudioEngine::setDelay(double seconds)
{
    m_player->setPropertyString("audio-delay", QString::number(seconds, 'f', 3));
}

QString AudioEngine::status() const
{
    const QVariantMap track = m_player->propertyValue("current-tracks/audio").toMap();
    if (track.isEmpty())
        return tr("No audio playing");
    const QString format = m_player->propertyString("audio-params/format");
    const QString codec = track.value(QStringLiteral("codec")).toString();
    if (format.startsWith(QLatin1String("spdif")))
        return tr("%1 → passthrough (bitstream to the receiver)").arg(codec.toUpper());
    const QString decoder = track.value(QStringLiteral("decoder")).toString();
    const QString channels = m_player->propertyString("audio-params/hr-channels");
    const int rate = m_player->propertyString("audio-params/samplerate").toInt();
    return tr("%1 decoded by %2 · %3 · %4 kHz")
        .arg(codec.toUpper(), decoder.isEmpty() ? tr("FFmpeg") : decoder, channels)
        .arg(rate / 1000.0, 0, 'g', 3);
}

void AudioEngine::reinitAudio()
{
    // Decoder and passthrough choices only take effect when the track is reopened.
    const QString aid = m_player->propertyString("aid");
    if (aid.isEmpty() || aid == QLatin1String("no") || aid == QLatin1String("auto"))
        return;
    syncFilters();
    m_player->setPropertyString("aid", QStringLiteral("no"));
    m_player->setPropertyString("aid", aid);
}

void AudioEngine::save()
{
    QSettings settings;
    settings.setValue(QStringLiteral("audio/eqEnabled"), m_eqEnabled);
    settings.setValue(QStringLiteral("audio/preamp"), m_preamp);
    settings.setValue(QStringLiteral("audio/gains"), toList(m_gains));
    settings.setValue(QStringLiteral("audio/nightMode"), m_nightMode);
    settings.setValue(QStringLiteral("audio/decoders"), m_decoders);
    settings.setValue(QStringLiteral("audio/passthrough"), m_passthrough);
    settings.setValue(QStringLiteral("audio/device"), m_device);
    settings.setValue(QStringLiteral("audio/channels"), m_channels);
    settings.setValue(QStringLiteral("audio/exclusive"), m_exclusive);
}

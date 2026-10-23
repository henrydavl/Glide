#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <array>

class MpvPlayer;

struct AudioDecoder {
    QString driver;      // e.g. "ac3_fixed"
    QString description; // e.g. "ATSC A/52A (AC-3)"
};

struct AudioCodecInfo {
    QString codec;       // FFmpeg codec name, e.g. "ac3"
    QString label;       // e.g. "AC-3 (Dolby Digital)"
    QString passthrough; // audio-spdif name, empty if the codec can't be bitstreamed
};

struct AudioDevice {
    QString name;
    QString description;
};

// Owns everything Glide does to audio: the equalizer / night-mode filter chain, decoder
// choice per codec, passthrough, output device and channel layout. State persists in
// QSettings and is pushed to mpv.
class AudioEngine : public QObject {
    Q_OBJECT
public:
    static constexpr int BandCount = 10;
    static const std::array<int, BandCount> Frequencies;
    using Gains = std::array<double, BandCount>;

    explicit AudioEngine(MpvPlayer *player, QObject *parent = nullptr);

    // Sets mpv options from saved settings; call before MpvPlayer::initialize().
    void configure();

    bool eqEnabled() const { return m_eqEnabled; }
    void setEqEnabled(bool enabled);
    double preamp() const { return m_preamp; }
    void setPreamp(double dB);
    const Gains &gains() const { return m_gains; }
    void setGain(int band, double dB);
    void setGains(const Gains &gains);
    bool nightMode() const { return m_nightMode; }
    void setNightMode(bool enabled);

    // Built-in presets first, then the user's saved ones.
    QStringList presetNames() const;
    bool isBuiltInPreset(const QString &name) const;
    Gains preset(const QString &name) const;
    void savePreset(const QString &name);
    void deletePreset(const QString &name);
    // The preset matching the current gains, or empty when they're custom.
    QString matchingPreset() const;

    static const QList<AudioCodecInfo> &codecs();
    QList<AudioDecoder> decodersFor(const QString &codec) const;
    QString decoderFor(const QString &codec) const; // empty = automatic
    void setDecoderFor(const QString &codec, const QString &driver);
    bool passthrough(const QString &format) const;
    void setPassthrough(const QString &format, bool enabled);

    QList<AudioDevice> devices() const;
    QString device() const { return m_device; }
    void setDevice(const QString &name);
    QString channels() const { return m_channels; }
    void setChannels(const QString &layout);
    bool exclusive() const { return m_exclusive; }
    void setExclusive(bool exclusive);
    double delay() const;
    void setDelay(double seconds);

    // One line describing what mpv is doing with the current audio track right now.
    QString status() const;

    // Live EQ tweaks go straight into the running filter graph. Before mpv rebuilds the
    // graph (new file, track switch) the stored filter string must catch up.
    void syncFilters();

signals:
    void eqChanged();

private:
    QString filterString() const;
    void applyFilters();
    bool sendToGraph(const QString &command, const QString &value, const QString &target);
    void reinitAudio();
    QString decoderPriority() const;
    void save();

    MpvPlayer *m_player;
    bool m_eqEnabled = false;
    double m_preamp = 0;
    Gains m_gains{};
    bool m_nightMode = false;
    bool m_filtersDirty = false;
    QVariantMap m_decoders;      // codec -> driver
    QStringList m_passthrough;   // audio-spdif formats
    QString m_device = QStringLiteral("auto");
    QString m_channels = QStringLiteral("auto-safe");
    bool m_exclusive = false;
};

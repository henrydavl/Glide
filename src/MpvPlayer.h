#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <mpv/client.h>

struct MpvTrack {
    qint64 id = 0;
    QString type; // "video", "audio" or "sub"
    QString title;
    QString lang;
    QString codec;
    int channels = 0;
    bool selected = false;
    bool external = false;

    QString label() const;
};

// Thin Qt wrapper around an mpv_handle. All calls happen on the GUI thread, and mpv's
// events are drained there too via the wakeup callback.
class MpvPlayer : public QObject {
    Q_OBJECT
public:
    explicit MpvPlayer(QObject *parent = nullptr);
    ~MpvPlayer() override;

    mpv_handle *handle() const { return m_mpv; }

    // Options must be set before initialize().
    void setOption(const char *name, const QString &value);
    void initialize();

    void loadFile(const QString &path, double startAt = 0);
    void command(const QStringList &args);
    void setPaused(bool paused);
    void setVolume(double volume);
    void seekTo(double seconds, bool exact);
    void showText(const QString &text, int durationMs = 2000);

    QString propertyString(const char *name) const;
    void setPropertyString(const char *name, const QString &value);

    double position() const { return m_position; }
    double duration() const { return m_duration; }
    bool isPaused() const { return m_paused; }
    double volume() const { return m_volume; }
    bool isMuted() const { return m_muted; }
    const QList<MpvTrack> &tracks() const { return m_tracks; }

signals:
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void pausedChanged(bool paused);
    void volumeChanged(double volume);
    void mutedChanged(bool muted);
    void tracksChanged();
    void fileLoaded();
    void endReached();
    void loadFailed(const QString &reason);

private:
    void processEvents();
    void handleProperty(const mpv_event_property *prop);

    mpv_handle *m_mpv = nullptr;
    double m_position = 0;
    double m_emittedPosition = -1;
    double m_duration = 0;
    double m_volume = 100;
    bool m_paused = false;
    bool m_muted = false;
    QList<MpvTrack> m_tracks;
};

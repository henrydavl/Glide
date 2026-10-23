#pragma once

#include "Icons.h"

#include <QElapsedTimer>
#include <QSlider>
#include <QWidget>

class QLabel;
class QToolButton;

class SeekSlider : public QSlider {
    Q_OBJECT
public:
    explicit SeekSlider(QWidget *parent = nullptr);

    void setDuration(double seconds);
    void setPosition(double seconds);
    bool isDragging() const { return m_dragging; }

signals:
    void seekRequested(double seconds, bool exact);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    double secondsAt(qreal x) const;

    double m_duration = 0;
    bool m_dragging = false;
    QElapsedTimer m_throttle;
};

class ControlBar : public QWidget {
    Q_OBJECT
public:
    explicit ControlBar(QWidget *parent = nullptr);

    void setPosition(double seconds);
    void setDuration(double seconds);
    void setPaused(bool paused);
    void setVolume(double volume);
    void setMuted(bool muted);
    void setFullscreen(bool fullscreen);

    QToolButton *audioButton() const { return m_audio; }
    QToolButton *subtitleButton() const { return m_subtitles; }

signals:
    void openClicked();
    void previousClicked();
    void playPauseClicked();
    void stopClicked();
    void nextClicked();
    void seekRequested(double seconds, bool exact);
    void volumeRequested(double volume);
    void muteClicked();
    void infoClicked();
    void playlistClicked();
    void fullscreenClicked();

private:
    QToolButton *makeButton(Icons::Name icon, const QString &tip);
    void updateTime();

    SeekSlider *m_seek;
    QLabel *m_time;
    QToolButton *m_play;
    QToolButton *m_mute;
    QToolButton *m_audio;
    QToolButton *m_subtitles;
    QToolButton *m_fullscreen;
    QSlider *m_volume;
    double m_position = 0;
    double m_duration = 0;
};

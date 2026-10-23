#include "ControlBar.h"

#include "MediaFiles.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>

SeekSlider::SeekSlider(QWidget *parent)
    : QSlider(Qt::Horizontal, parent)
{
    setObjectName(QStringLiteral("Seek"));
    setRange(0, 0);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setFixedHeight(18);
    m_throttle.start();
}

void SeekSlider::setDuration(double seconds)
{
    m_duration = std::max(0.0, seconds);
    setRange(0, int(m_duration * 10));
}

void SeekSlider::setPosition(double seconds)
{
    setValue(int(seconds * 10));
}

double SeekSlider::secondsAt(qreal x) const
{
    if (m_duration <= 0 || width() <= 0)
        return 0;
    return std::clamp(x / width(), 0.0, 1.0) * m_duration;
}

void SeekSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_duration <= 0)
        return;
    m_dragging = true;
    const double seconds = secondsAt(event->position().x());
    setPosition(seconds);
    emit seekRequested(seconds, false);
    m_throttle.restart();
}

void SeekSlider::mouseMoveEvent(QMouseEvent *event)
{
    const double seconds = secondsAt(event->position().x());
    if (m_duration > 0)
        QToolTip::showText(event->globalPosition().toPoint() + QPoint(0, -36), formatTime(seconds), this);
    if (!m_dragging)
        return;
    setPosition(seconds);
    if (m_throttle.elapsed() > 80) {
        emit seekRequested(seconds, false);
        m_throttle.restart();
    }
}

void SeekSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    m_dragging = false;
    const double seconds = secondsAt(event->position().x());
    setPosition(seconds);
    emit seekRequested(seconds, true);
}

ControlBar::ControlBar(QWidget *parent)
    : QWidget(parent)
    , m_seek(new SeekSlider(this))
    , m_time(new QLabel(this))
    , m_volume(new QSlider(Qt::Horizontal, this))
{
    setObjectName(QStringLiteral("ControlBar"));
    setAttribute(Qt::WA_StyledBackground);

    auto *open = makeButton(Icons::Name::Open, tr("Open file (Ctrl+O)"));
    auto *previous = makeButton(Icons::Name::Previous, tr("Previous (Page Up)"));
    m_play = makeButton(Icons::Name::Play, tr("Play / Pause (Space)"));
    m_play->setIconSize(QSize(26, 26));
    auto *stop = makeButton(Icons::Name::Stop, tr("Stop"));
    auto *next = makeButton(Icons::Name::Next, tr("Next (Page Down)"));
    m_audio = makeButton(Icons::Name::Audio, tr("Audio track (A)"));
    m_subtitles = makeButton(Icons::Name::Subtitle, tr("Subtitles (S)"));
    auto *info = makeButton(Icons::Name::Info, tr("Playback statistics (Tab)"));
    m_mute = makeButton(Icons::Name::Volume, tr("Mute (M)"));
    auto *playlist = makeButton(Icons::Name::Playlist, tr("Playlist (F6)"));
    m_fullscreen = makeButton(Icons::Name::Fullscreen, tr("Fullscreen (Enter)"));
    m_audio->setPopupMode(QToolButton::InstantPopup);
    m_subtitles->setPopupMode(QToolButton::InstantPopup);

    m_time->setObjectName(QStringLiteral("Time"));
    m_volume->setObjectName(QStringLiteral("Volume"));
    m_volume->setRange(0, 100);
    m_volume->setFixedWidth(90);
    m_volume->setFocusPolicy(Qt::NoFocus);
    updateTime();

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    row->addWidget(open);
    row->addSpacing(6);
    row->addWidget(previous);
    row->addWidget(m_play);
    row->addWidget(stop);
    row->addWidget(next);
    row->addSpacing(12);
    row->addWidget(m_time);
    row->addStretch(1);
    row->addWidget(m_audio);
    row->addWidget(m_subtitles);
    row->addWidget(info);
    row->addSpacing(8);
    row->addWidget(m_mute);
    row->addWidget(m_volume);
    row->addSpacing(8);
    row->addWidget(playlist);
    row->addWidget(m_fullscreen);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 4, 10, 6);
    layout->setSpacing(2);
    layout->addWidget(m_seek);
    layout->addLayout(row);

    connect(open, &QToolButton::clicked, this, &ControlBar::openClicked);
    connect(previous, &QToolButton::clicked, this, &ControlBar::previousClicked);
    connect(m_play, &QToolButton::clicked, this, &ControlBar::playPauseClicked);
    connect(stop, &QToolButton::clicked, this, &ControlBar::stopClicked);
    connect(next, &QToolButton::clicked, this, &ControlBar::nextClicked);
    connect(info, &QToolButton::clicked, this, &ControlBar::infoClicked);
    connect(m_mute, &QToolButton::clicked, this, &ControlBar::muteClicked);
    connect(playlist, &QToolButton::clicked, this, &ControlBar::playlistClicked);
    connect(m_fullscreen, &QToolButton::clicked, this, &ControlBar::fullscreenClicked);
    connect(m_seek, &SeekSlider::seekRequested, this, &ControlBar::seekRequested);
    connect(m_volume, &QSlider::valueChanged, this, [this](int value) { emit volumeRequested(value); });
}

QToolButton *ControlBar::makeButton(Icons::Name icon, const QString &tip)
{
    auto *button = new QToolButton(this);
    button->setIcon(Icons::icon(icon));
    button->setIconSize(QSize(20, 20));
    button->setToolTip(tip);
    button->setAutoRaise(true);
    // Buttons never take focus, so Space always means play/pause.
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

void ControlBar::setPosition(double seconds)
{
    m_position = seconds;
    if (!m_seek->isDragging())
        m_seek->setPosition(seconds);
    updateTime();
}

void ControlBar::setDuration(double seconds)
{
    m_duration = seconds;
    m_seek->setDuration(seconds);
    updateTime();
}

void ControlBar::setPaused(bool paused)
{
    m_play->setIcon(Icons::icon(paused ? Icons::Name::Play : Icons::Name::Pause));
}

void ControlBar::setVolume(double volume)
{
    const QSignalBlocker blocker(m_volume);
    m_volume->setValue(int(volume));
    m_volume->setToolTip(tr("Volume %1%").arg(int(volume)));
}

void ControlBar::setMuted(bool muted)
{
    m_mute->setIcon(Icons::icon(muted ? Icons::Name::Mute : Icons::Name::Volume));
}

void ControlBar::setFullscreen(bool fullscreen)
{
    m_fullscreen->setIcon(Icons::icon(fullscreen ? Icons::Name::ExitFullscreen : Icons::Name::Fullscreen));
}

void ControlBar::updateTime()
{
    const bool hours = m_duration >= 3600;
    m_time->setText(QStringLiteral("%1 / %2").arg(formatTime(m_position, hours), formatTime(m_duration, hours)));
}

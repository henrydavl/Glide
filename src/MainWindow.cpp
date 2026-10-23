#include "MainWindow.h"

#include "ControlBar.h"
#include "DurationProber.h"
#include "MediaFiles.h"
#include "MpvPlayer.h"
#include "PlaylistPanel.h"
#include "VideoView.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QSettings>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cstdlib>
#include <utility>

namespace {

// Cheaper scalers for weak iGPUs; everything else in mpv's pipeline stays the same.
const std::pair<const char *, const char *> LowPowerOptions[] = {
    {"scale", "bilinear"},
    {"dscale", "bilinear"},
    {"cscale", "bilinear"},
    {"dither", "no"},
    {"correct-downscaling", "no"},
    {"linear-downscaling", "no"},
    {"sigmoid-upscaling", "no"},
    {"deband", "no"},
    {"hdr-compute-peak", "no"},
};

QString lastDirectory()
{
    return QSettings().value(QStringLiteral("lastDir"), QDir::homePath()).toString();
}

void rememberDirectory(const QString &path)
{
    const QFileInfo info(path);
    QSettings().setValue(QStringLiteral("lastDir"), info.isDir() ? info.absoluteFilePath() : info.absolutePath());
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
    , m_player(new MpvPlayer(this))
    , m_view(createVideoView(this))
    , m_playlist(new PlaylistPanel(&m_store, this))
    , m_controls(new ControlBar(this))
    , m_prober(new DurationProber(this))
{
    setWindowTitle(QStringLiteral("Glide"));
    setAcceptDrops(true);
    setMinimumSize(640, 400);
    resize(1280, 760);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_view->widget());
    m_splitter->addWidget(m_playlist);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setCollapsible(0, false);
    m_splitter->setHandleWidth(1);
    m_splitter->setSizes({960, 320});

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_splitter, 1);
    layout->addWidget(m_controls);

    configurePlayer();
    m_view->attach(m_player);
    m_player->initialize();

    m_resumeTimer.setInterval(5000);
    m_idleTimer.setInterval(2000);
    m_idleTimer.setSingleShot(true);

    connectSignals();
    restoreSession();
    m_view->widget()->setFocus();
    qApp->installEventFilter(this);
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    // The render context must go before the mpv core it belongs to.
    m_view->detach();
    delete m_player;
    m_player = nullptr;
}

void MainWindow::configurePlayer()
{
    const QSettings settings;
    const std::pair<const char *, QString> options[] = {
        // GPU decoding (D3D11VA on Windows) with automatic fallback to software for
        // codecs or profiles the GPU can't handle.
        {"hwdec", settings.value(QStringLiteral("video/hwdec"), QStringLiteral("auto-safe")).toString()},
        // Broadcast .ts is very often 1080i. "auto" deinterlaces only frames flagged as
        // interlaced, and with hwdec on Windows it runs on the GPU (d3d11vpp), not the CPU.
        {"deinterlace", settings.value(QStringLiteral("video/deinterlace"), QStringLiteral("auto")).toString()},
        {"keep-open", QStringLiteral("yes")},
        {"idle", QStringLiteral("yes")},
        {"input-default-bindings", QStringLiteral("no")},
        {"input-vo-keyboard", QStringLiteral("no")},
        // Read far ahead so a slow USB/network drive or a bitrate spike can't starve
        // the decoder mid-scene.
        {"cache", QStringLiteral("yes")},
        {"demuxer-max-bytes", QStringLiteral("512MiB")},
        {"demuxer-max-back-bytes", QStringLiteral("128MiB")},
        {"demuxer-readahead-secs", QStringLiteral("30")},
        // TV and IPTV recordings often announce audio/subtitle streams late; probe deeper
        // so they're found at open instead of popping in (or missing) later.
        {"demuxer-lavf-probesize", QStringLiteral("5000000")},
        {"demuxer-lavf-analyzeduration", QStringLiteral("5")},
        {"sub-auto", QStringLiteral("fuzzy")},
        {"ytdl", QStringLiteral("no")},
        {"osd-font-size", QStringLiteral("30")},
        {"osd-duration", QStringLiteral("1500")},
        {"volume-max", QStringLiteral("150")},
        {"volume", QString::number(settings.value(QStringLiteral("audio/volume"), 100).toDouble())},
    };
    for (const auto &[name, value] : options)
        m_player->setOption(name, value);
}

void MainWindow::connectSignals()
{
    connect(m_player, &MpvPlayer::positionChanged, m_controls, &ControlBar::setPosition);
    connect(m_player, &MpvPlayer::durationChanged, this, [this](double seconds) {
        m_controls->setDuration(seconds);
        if (seconds > 0 && !m_currentPath.isEmpty()) {
            m_store.setDuration(m_currentPath, seconds);
            m_playlist->refresh(m_currentPath);
        }
    });
    connect(m_player, &MpvPlayer::pausedChanged, m_controls, &ControlBar::setPaused);
    connect(m_player, &MpvPlayer::volumeChanged, m_controls, &ControlBar::setVolume);
    connect(m_player, &MpvPlayer::mutedChanged, m_controls, &ControlBar::setMuted);
    connect(m_player, &MpvPlayer::fileLoaded, this, [this] {
        m_fileReady = true;
        if (!m_pendingOsd.isEmpty()) {
            m_player->showText(m_pendingOsd);
            m_pendingOsd.clear();
        }
    });
    connect(m_player, &MpvPlayer::endReached, this, [this] {
        saveResume();
        const QString next = m_playlist->neighbour(1);
        if (!next.isEmpty())
            play(next);
    });
    connect(m_player, &MpvPlayer::loadFailed, this, [this](const QString &reason) {
        m_player->showText(tr("Cannot play %1: %2").arg(QFileInfo(m_currentPath).fileName(), reason), 4000);
    });

    connect(m_controls, &ControlBar::openClicked, this, &MainWindow::openFileDialog);
    connect(m_controls, &ControlBar::previousClicked, this, [this] { playNeighbour(-1); });
    connect(m_controls, &ControlBar::playPauseClicked, this, &MainWindow::togglePause);
    connect(m_controls, &ControlBar::stopClicked, this, &MainWindow::stop);
    connect(m_controls, &ControlBar::nextClicked, this, [this] { playNeighbour(1); });
    connect(m_controls, &ControlBar::seekRequested, m_player, &MpvPlayer::seekTo);
    connect(m_controls, &ControlBar::volumeRequested, m_player, &MpvPlayer::setVolume);
    connect(m_controls, &ControlBar::muteClicked, this, [this] { m_player->command({"cycle", "mute"}); });
    connect(m_controls, &ControlBar::infoClicked, this, &MainWindow::toggleStats);
    connect(m_controls, &ControlBar::playlistClicked, this, &MainWindow::togglePlaylist);
    connect(m_controls, &ControlBar::fullscreenClicked, this, &MainWindow::toggleFullscreen);

    auto *audioMenu = new QMenu(this);
    m_controls->audioButton()->setMenu(audioMenu);
    connect(audioMenu, &QMenu::aboutToShow, this, [this, audioMenu] { fillTrackMenu(audioMenu, QStringLiteral("audio")); });
    auto *subtitleMenu = new QMenu(this);
    m_controls->subtitleButton()->setMenu(subtitleMenu);
    connect(subtitleMenu, &QMenu::aboutToShow, this, [this, subtitleMenu] { fillTrackMenu(subtitleMenu, QStringLiteral("sub")); });

    connect(m_playlist, &PlaylistPanel::playRequested, this, &MainWindow::play);
    connect(m_playlist, &PlaylistPanel::addRequested, this, &MainWindow::addFilesDialog);
    connect(m_prober, &DurationProber::probed, this, [this](const QString &path, double seconds) {
        m_store.setDuration(path, seconds);
        m_playlist->refresh(path);
    });

    connect(&m_resumeTimer, &QTimer::timeout, this, &MainWindow::saveResume);
    connect(&m_idleTimer, &QTimer::timeout, this, &MainWindow::hideIdleControls);
    m_resumeTimer.start();
}

void MainWindow::restoreSession()
{
    const QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    m_splitter->restoreState(settings.value(QStringLiteral("window/splitter")).toByteArray());
    m_playlist->setVisible(settings.value(QStringLiteral("window/playlist"), true).toBool());
    m_controls->setVolume(settings.value(QStringLiteral("audio/volume"), 100).toDouble());
    m_controls->setPaused(true);
    if (settings.value(QStringLiteral("video/lowPower"), false).toBool())
        setLowPower(true);

    // Bring back the last playlist, ready to continue with Space.
    QStringList files;
    for (const QString &path : settings.value(QStringLiteral("session/playlist")).toStringList()) {
        if (QFileInfo::exists(path))
            files << path;
    }
    m_playlist->setFiles(files);
    const QString current = settings.value(QStringLiteral("session/current")).toString();
    if (files.contains(current)) {
        m_currentPath = current;
        m_playlist->setCurrent(current);
        setWindowTitle(QStringLiteral("%1 — Glide").arg(QFileInfo(current).fileName()));
    }
    probeDurations(files);
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/splitter"), m_splitter->saveState());
    settings.setValue(QStringLiteral("window/playlist"), m_playlist->isVisible());
    settings.setValue(QStringLiteral("audio/volume"), m_player->volume());
    settings.setValue(QStringLiteral("video/hwdec"), m_player->propertyString("hwdec"));
    settings.setValue(QStringLiteral("video/deinterlace"), m_player->propertyString("deinterlace"));
    settings.setValue(QStringLiteral("video/lowPower"), m_lowPower);
    settings.setValue(QStringLiteral("session/playlist"), m_playlist->files());
    settings.setValue(QStringLiteral("session/current"), m_currentPath);
}

void MainWindow::openPaths(const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    if (paths.size() == 1 && isSubtitleFile(paths.first()) && !m_currentPath.isEmpty()) {
        m_player->command({"sub-add", paths.first(), "select"});
        return;
    }

    QStringList videos;
    QString first;
    if (paths.size() == 1 && QFileInfo(paths.first()).isFile()) {
        const QFileInfo file(paths.first());
        first = file.absoluteFilePath();
        videos = videosInFolder(file.absolutePath());
        if (!videos.contains(first))
            videos.prepend(first);
    } else {
        for (const QString &path : paths) {
            const QFileInfo info(path);
            if (info.isDir())
                videos << videosInFolder(info.absoluteFilePath());
            else if (info.isFile())
                videos << info.absoluteFilePath();
        }
        videos.removeDuplicates();
        first = videos.value(0);
    }
    if (first.isEmpty())
        return;

    rememberDirectory(first);
    m_prober->clear();
    m_playlist->setFiles(videos);
    play(first);
    probeDurations(videos);
}

void MainWindow::probeDurations(const QStringList &paths)
{
    QStringList unknown;
    for (const QString &path : paths) {
        if (m_store.duration(path) == 0 && path != m_currentPath)
            unknown << path;
    }
    m_prober->enqueue(unknown);
}

void MainWindow::play(const QString &path)
{
    saveResume();
    m_currentPath = path;
    m_fileReady = false;
    m_stopped = false;

    const double resume = m_store.resumePosition(path);
    m_player->loadFile(path, resume);
    m_player->setPaused(false);
    m_pendingOsd = resume > 0 ? tr("Resumed at %1").arg(formatTime(resume)) : QString();

    m_playlist->setCurrent(path);
    setWindowTitle(QStringLiteral("%1 — Glide").arg(QFileInfo(path).fileName()));
}

void MainWindow::playNeighbour(int step)
{
    const QString path = m_playlist->neighbour(step);
    if (!path.isEmpty())
        play(path);
    else
        m_player->showText(step > 0 ? tr("End of playlist") : tr("Start of playlist"));
}

void MainWindow::togglePause()
{
    if (m_currentPath.isEmpty()) {
        openFileDialog();
        return;
    }
    if (m_stopped) {
        play(m_currentPath);
        return;
    }
    m_player->command({"cycle", "pause"});
}

void MainWindow::stop()
{
    saveResume();
    m_stopped = true;
    m_fileReady = false;
    m_player->command({"stop"});
    m_controls->setPaused(true);
    m_controls->setPosition(0);
}

void MainWindow::saveResume()
{
    if (m_currentPath.isEmpty() || !m_fileReady)
        return;
    const double position = m_player->position();
    const double duration = m_player->duration();
    if (duration <= 0)
        return;
    // Nothing worth resuming in the first seconds, and a finished file starts over.
    const bool worthKeeping = position > 10 && position < duration - 20;
    m_store.setResumePosition(m_currentPath, worthKeeping ? position : 0);
    m_playlist->refresh(m_currentPath);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    QWidget *video = m_view->widget();
    switch (event->type()) {
    case QEvent::KeyPress: {
        // Key events bubble up through parents; only act on the first delivery.
        QWidget *target = QApplication::focusWidget() ? QApplication::focusWidget() : this;
        if (watched == target && target->window() == this && handleKey(static_cast<QKeyEvent *>(event)))
            return true;
        break;
    }
    case QEvent::FileOpen:
        if (watched == qApp) {
            openPaths({static_cast<QFileOpenEvent *>(event)->file()});
            return true;
        }
        break;
    case QEvent::MouseButtonPress:
        if (watched == video)
            video->setFocus();
        break;
    case QEvent::MouseButtonDblClick:
        if (watched == video && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            toggleFullscreen();
            return true;
        }
        break;
    case QEvent::MouseMove:
        if (watched == video || watched == m_controls)
            onVideoMouseMove();
        break;
    case QEvent::Wheel:
        if (watched == video) {
            // Trackpads send many small deltas; step once per notch's worth.
            m_wheelAccumulator += static_cast<QWheelEvent *>(event)->angleDelta().y();
            while (std::abs(m_wheelAccumulator) >= 120) {
                changeVolume(m_wheelAccumulator > 0 ? 5 : -5);
                m_wheelAccumulator += m_wheelAccumulator > 0 ? -120 : 120;
            }
            return true;
        }
        break;
    case QEvent::ContextMenu:
        if (watched == video) {
            showContextMenu(static_cast<QContextMenuEvent *>(event)->globalPos());
            return true;
        }
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

bool MainWindow::handleKey(QKeyEvent *event)
{
    QWidget *focus = QApplication::focusWidget();
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const bool ctrl = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    const bool alt = mods & Qt::AltModifier;

    if (qobject_cast<QLineEdit *>(focus)) {
        if (key != Qt::Key_Escape)
            return false;
        m_view->widget()->setFocus();
        return true;
    }
    if (focus == m_playlist->view() && mods == Qt::NoModifier) {
        switch (key) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            return false;
        default:
            break;
        }
    }

    switch (key) {
    case Qt::Key_Space:
        togglePause();
        return true;
    case Qt::Key_Left:
    case Qt::Key_Right: {
        const double step = ctrl ? 30 : alt ? 60 : shift ? 1 : 5;
        seekBy(key == Qt::Key_Left ? -step : step);
        return true;
    }
    case Qt::Key_Up:
        changeVolume(5);
        return true;
    case Qt::Key_Down:
        changeVolume(-5);
        return true;
    case Qt::Key_M:
        m_player->command({"osd-msg", "cycle", "mute"});
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_F:
        toggleFullscreen();
        return true;
    case Qt::Key_Escape:
        if (!isFullScreen())
            return false;
        setFullscreen(false);
        return true;
    case Qt::Key_PageDown:
        playNeighbour(1);
        return true;
    case Qt::Key_PageUp:
        playNeighbour(-1);
        return true;
    case Qt::Key_F6:
    case Qt::Key_L:
        togglePlaylist();
        return true;
    case Qt::Key_Tab:
        toggleStats();
        return true;
    case Qt::Key_I:
        showQuickInfo();
        return true;
    case Qt::Key_A:
        m_player->command({"osd-msg", "cycle", "aid"});
        return true;
    case Qt::Key_S:
        if (ctrl)
            return false;
        m_player->command({"osd-msg", "cycle", "sid"});
        return true;
    case Qt::Key_D:
        m_player->command({"osd-msg", "cycle-values", "deinterlace", "auto", "yes", "no"});
        return true;
    case Qt::Key_H:
        m_player->command({"osd-msg", "cycle-values", "hwdec", "auto-safe", "no"});
        return true;
    case Qt::Key_Period:
        m_player->command({"frame-step"});
        return true;
    case Qt::Key_Comma:
        m_player->command({"frame-back-step"});
        return true;
    case Qt::Key_O:
        if (!ctrl)
            return false;
        shift ? openFolderDialog() : openFileDialog();
        return true;
    case Qt::Key_Q:
    case Qt::Key_W:
        if (!ctrl)
            return false;
        close();
        return true;
    default:
        return false;
    }
}

void MainWindow::seekBy(double seconds)
{
    m_player->command({"osd-msg-bar", "seek", QString::number(seconds), "relative"});
}

void MainWindow::changeVolume(double delta)
{
    m_player->command({"osd-msg-bar", "add", "volume", QString::number(delta)});
}

void MainWindow::toggleFullscreen()
{
    setFullscreen(!isFullScreen());
}

void MainWindow::setFullscreen(bool on)
{
    if (on == isFullScreen())
        return;
    if (on) {
        m_playlistBeforeFullscreen = m_playlist->isVisible();
        m_playlist->hide();
        m_controls->hide();
        showFullScreen();
        m_idleTimer.start();
    } else {
        showNormal();
        m_controls->show();
        m_playlist->setVisible(m_playlistBeforeFullscreen);
        m_view->widget()->unsetCursor();
        m_idleTimer.stop();
    }
    m_controls->setFullscreen(on);
}

void MainWindow::togglePlaylist()
{
    if (isFullScreen()) {
        m_playlistBeforeFullscreen = !m_playlistBeforeFullscreen;
        return;
    }
    m_playlist->setVisible(!m_playlist->isVisible());
}

void MainWindow::onVideoMouseMove()
{
    if (!isFullScreen())
        return;
    m_view->widget()->unsetCursor();
    if (mapFromGlobal(QCursor::pos()).y() > height() - 140)
        m_controls->show();
    m_idleTimer.start();
}

void MainWindow::hideIdleControls()
{
    if (!isFullScreen())
        return;
    if (m_controls->isVisible() && m_controls->underMouse()) {
        m_idleTimer.start();
        return;
    }
    m_controls->hide();
    m_view->widget()->setCursor(Qt::BlankCursor);
}

void MainWindow::showContextMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    menu.addAction(tr("Open file…"), this, &MainWindow::openFileDialog);
    menu.addAction(tr("Open folder…"), this, &MainWindow::openFolderDialog);
    menu.addSeparator();
    QMenu *audio = menu.addMenu(tr("Audio"));
    fillTrackMenu(audio, QStringLiteral("audio"));
    QMenu *subtitles = menu.addMenu(tr("Subtitles"));
    fillTrackMenu(subtitles, QStringLiteral("sub"));
    fillVideoMenu(menu.addMenu(tr("Video")));
    menu.addSeparator();

    QAction *playlist = menu.addAction(tr("Playlist\tF6"), this, &MainWindow::togglePlaylist);
    playlist->setCheckable(true);
    playlist->setChecked(isFullScreen() ? m_playlistBeforeFullscreen : m_playlist->isVisible());
    QAction *fullscreen = menu.addAction(tr("Fullscreen\tEnter"), this, &MainWindow::toggleFullscreen);
    fullscreen->setCheckable(true);
    fullscreen->setChecked(isFullScreen());
    menu.addAction(tr("Playback statistics\tTab"), this, &MainWindow::toggleStats);
    menu.addAction(tr("Media info\tI"), this, &MainWindow::showQuickInfo);
    menu.exec(globalPos);
}

void MainWindow::fillTrackMenu(QMenu *menu, const QString &type)
{
    menu->clear();
    const bool subtitles = type == QLatin1String("sub");
    const char *property = subtitles ? "sid" : "aid";
    auto *group = new QActionGroup(menu);

    bool anySelected = false;
    QList<QAction *> trackActions;
    for (const MpvTrack &track : m_player->tracks()) {
        if (track.type != type)
            continue;
        QAction *action = menu->addAction(track.label());
        action->setCheckable(true);
        action->setChecked(track.selected);
        anySelected |= track.selected;
        group->addAction(action);
        const qint64 id = track.id;
        connect(action, &QAction::triggered, this, [this, property, id] {
            m_player->setPropertyString(property, QString::number(id));
        });
        trackActions << action;
    }

    if (subtitles) {
        QAction *off = new QAction(tr("Off"), menu);
        off->setCheckable(true);
        off->setChecked(!anySelected);
        group->addAction(off);
        connect(off, &QAction::triggered, this, [this] { m_player->setPropertyString("sid", QStringLiteral("no")); });
        menu->insertAction(trackActions.value(0), off);
        if (!trackActions.isEmpty())
            menu->insertSeparator(trackActions.first());
        menu->addSeparator();
        menu->addAction(tr("Load subtitle file…"), this, &MainWindow::loadSubtitleDialog);
    } else if (trackActions.isEmpty()) {
        menu->addAction(tr("No audio tracks"))->setEnabled(false);
    }
}

void MainWindow::fillVideoMenu(QMenu *menu)
{
    const QString deinterlace = m_player->propertyString("deinterlace");
    auto *group = new QActionGroup(menu);
    const std::pair<QString, QString> modes[] = {
        {QStringLiteral("auto"), tr("Deinterlace: automatic")},
        {QStringLiteral("yes"), tr("Deinterlace: always")},
        {QStringLiteral("no"), tr("Deinterlace: off")},
    };
    for (const auto &mode : modes) {
        const QString value = mode.first;
        QAction *action = menu->addAction(mode.second, this, [this, value] {
            m_player->setPropertyString("deinterlace", value);
        });
        action->setCheckable(true);
        action->setChecked(deinterlace == value);
        group->addAction(action);
    }
    menu->addSeparator();

    QAction *hwdec = menu->addAction(tr("Hardware decoding\tH"), this, [this](bool on) {
        m_player->setPropertyString("hwdec", on ? QStringLiteral("auto-safe") : QStringLiteral("no"));
    });
    hwdec->setCheckable(true);
    hwdec->setChecked(m_player->propertyString("hwdec") != QLatin1String("no"));

    QAction *lowPower = menu->addAction(tr("Low-power rendering"), this, &MainWindow::setLowPower);
    lowPower->setCheckable(true);
    lowPower->setChecked(m_lowPower);
}

void MainWindow::showQuickInfo()
{
    // mpv fills in each ${property} at display time, so this reflects its live state.
    const QString text = QStringLiteral(
        "${filename}\n"
        "Container: ${file-format}\n"
        "Video: ${video-format} ${video-params/w}×${video-params/h} @ ${container-fps} fps"
        "${?video-frame-info/interlaced==yes: (interlaced)}\n"
        "Decoder: ${hwdec-current} · Deinterlace: ${deinterlace}\n"
        "Audio: ${audio-codec-name} ${audio-params/channel-count}ch\n"
        "Dropped frames: ${frame-drop-count} · Cache: ${demuxer-cache-duration}s\n"
        "Renderer: ") + m_view->backendName();
    m_player->command({"expand-properties", "show-text", text, "6000"});
}

void MainWindow::toggleStats()
{
    m_player->command({"script-binding", "stats/display-stats-toggle"});
}

void MainWindow::setLowPower(bool on)
{
    if (on == m_lowPower)
        return;
    m_lowPower = on;
    for (const auto &[name, fastValue] : LowPowerOptions) {
        const QString key = QString::fromLatin1(name);
        if (on) {
            m_qualityOptions.insert(key, m_player->propertyString(name));
            m_player->setPropertyString(name, QString::fromLatin1(fastValue));
        } else if (m_qualityOptions.contains(key)) {
            m_player->setPropertyString(name, m_qualityOptions.value(key));
        }
    }
    m_player->showText(on ? tr("Low-power rendering on") : tr("Low-power rendering off"));
}

void MainWindow::openFileDialog()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Open video"), lastDirectory(),
        tr("Videos (%1);;All files (*)").arg(videoFilePatterns()));
    if (!files.isEmpty())
        openPaths(files);
}

void MainWindow::openFolderDialog()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Open folder"), lastDirectory());
    if (!folder.isEmpty())
        openPaths({folder});
}

void MainWindow::addFilesDialog()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add to playlist"), lastDirectory(),
        tr("Videos (%1);;All files (*)").arg(videoFilePatterns()));
    if (files.isEmpty())
        return;
    rememberDirectory(files.first());
    m_playlist->addFiles(files);
    probeDurations(files);
}

void MainWindow::loadSubtitleDialog()
{
    const QString start = m_currentPath.isEmpty() ? lastDirectory() : QFileInfo(m_currentPath).absolutePath();
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Load subtitle"), start, tr("Subtitles (*.srt *.ass *.ssa *.sub *.vtt *.sup *.smi);;All files (*)"));
    if (!file.isEmpty())
        m_player->command({"sub-add", file, "select"});
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    setFullscreen(false);
    saveResume();
    saveSettings();
    QWidget::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            paths << url.toLocalFile();
    }
    openPaths(paths);
    event->acceptProposedAction();
}

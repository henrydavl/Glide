#pragma once

#include "MediaStore.h"

#include <QHash>
#include <QTimer>
#include <QWidget>

class AudioDialog;
class AudioEngine;
class ControlBar;
class DurationProber;
class MpvPlayer;
class PlaylistPanel;
class QKeyEvent;
class QMenu;
class QSplitter;
class VideoView;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // A single file opens its whole folder into the playlist; folders and multiple
    // files replace the playlist with what was given.
    void openPaths(const QStringList &paths);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void configurePlayer();
    void connectSignals();
    void restoreSession();
    void saveSettings();

    void play(const QString &path);
    void playNeighbour(int step);
    void togglePause();
    void stop();
    void saveResume();
    void probeDurations(const QStringList &paths);

    bool handleKey(QKeyEvent *event);
    void seekBy(double seconds);
    void changeVolume(double delta);
    void toggleFullscreen();
    void setFullscreen(bool on);
    void togglePlaylist();
    void onVideoMouseMove();
    void hideIdleControls();

    void showContextMenu(const QPoint &globalPos);
    void fillTrackMenu(QMenu *menu, const QString &type);
    void fillVideoMenu(QMenu *menu);
    void showQuickInfo();
    void toggleStats();
    void showAudioDialog(int tab);
    void selectAudioTrack(const QString &id);
    void setLowPower(bool on);

    void openFileDialog();
    void openFolderDialog();
    void addFilesDialog();
    void loadSubtitleDialog();

    MediaStore m_store;
    MpvPlayer *m_player;
    VideoView *m_view;
    PlaylistPanel *m_playlist;
    ControlBar *m_controls;
    DurationProber *m_prober;
    AudioEngine *m_audio;
    AudioDialog *m_audioDialog = nullptr;
    QSplitter *m_splitter = nullptr;

    QTimer m_resumeTimer;
    QTimer m_idleTimer;
    QString m_currentPath;
    QString m_pendingOsd;
    bool m_fileReady = false;
    bool m_stopped = true;
    bool m_playlistBeforeFullscreen = true;
    bool m_lowPower = false;
    int m_wheelAccumulator = 0;
    QHash<QString, QString> m_qualityOptions;
};

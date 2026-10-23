#pragma once

#include <QString>

class MpvPlayer;
class QWidget;

// The surface mpv draws into. Windows embeds mpv's own D3D11 window (fastest path);
// macOS/Linux render through libmpv's OpenGL render API (used for development).
class VideoView {
public:
    virtual ~VideoView() = default;

    virtual QWidget *widget() = 0;
    // Called before MpvPlayer::initialize(); sets the vo and backend-specific options.
    virtual void attach(MpvPlayer *player) = 0;
    // Called before the player is destroyed.
    virtual void detach() {}
    virtual QString backendName() const = 0;
};

// GLIDE_VIDEO=native|gl overrides the platform default.
bool preferNativeVideo();
VideoView *createVideoView(QWidget *parent);

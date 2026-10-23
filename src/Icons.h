#pragma once

#include <QIcon>

namespace Icons {

enum class Name {
    Open,
    Previous,
    Play,
    Pause,
    Stop,
    Next,
    Volume,
    Mute,
    Audio,
    Subtitle,
    Info,
    Playlist,
    Fullscreen,
    ExitFullscreen,
};

// Drawn in code so the app needs no image assets or the QtSvg module.
QIcon icon(Name name);

} // namespace Icons

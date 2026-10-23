cmake_minimum_required(VERSION 3.21)
project(Glide VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

qt_add_executable(Glide WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/MainWindow.cpp src/MainWindow.h
    src/MpvPlayer.cpp src/MpvPlayer.h
    src/VideoView.cpp src/VideoView.h
    src/ControlBar.cpp src/ControlBar.h
    src/PlaylistPanel.cpp src/PlaylistPanel.h
    src/DurationProber.cpp src/DurationProber.h
    src/MediaStore.cpp src/MediaStore.h
    src/MediaFiles.cpp src/MediaFiles.h
    src/Icons.cpp src/Icons.h
)

target_link_libraries(Glide PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network PkgConfig::MPV)

# Window/taskbar icon at runtime. Regenerate all icon files with scripts/make-icon.py.
qt_add_resources(Glide "icons" PREFIX "/" FILES assets/icon-256.png)

if(WIN32)
    target_sources(Glide PRIVATE src/glide.rc)
elseif(APPLE)
    set(GLIDE_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/assets/Glide.icns)
    set_source_files_properties(${GLIDE_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(Glide PRIVATE ${GLIDE_ICNS})
endif()

set_target_properties(Glide PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER dev.glide.player
    MACOSX_BUNDLE_BUNDLE_NAME Glide
    MACOSX_BUNDLE_ICON_FILE Glide.icns
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

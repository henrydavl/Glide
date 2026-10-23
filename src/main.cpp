#include "MainWindow.h"
#include "VideoView.h"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPalette>
#include <QProxyStyle>
#include <QSettings>
#include <QStyleFactory>
#include <QSurfaceFormat>

#include <clocale>
#include <memory>

namespace {

QString instanceName()
{
    return QStringLiteral("glide-%1").arg(qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER")));
}

// Double-clicking another video in Explorer hands it to the open window, like PotPlayer.
bool forwardToRunningInstance(const QStringList &files)
{
    QLocalSocket socket;
    socket.connectToServer(instanceName());
    if (!socket.waitForConnected(300))
        return false;
    socket.write(files.join('\n').toUtf8());
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState)
        socket.waitForDisconnected(1000);
    return true;
}

void listenForOtherInstances(QLocalServer &server, MainWindow &window)
{
    QLocalServer::removeServer(instanceName());
    server.listen(instanceName());
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window] {
        while (QLocalSocket *client = server.nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(client, &QLocalSocket::readyRead, client, [client, buffer] { buffer->append(client->readAll()); });
            QObject::connect(client, &QLocalSocket::disconnected, &window, [client, buffer, &window] {
                buffer->append(client->readAll());
                client->deleteLater();
                const QStringList paths = QString::fromUtf8(*buffer).split('\n', Qt::SkipEmptyParts);
                window.openPaths(paths);
                if (window.isMinimized())
                    window.showNormal();
                window.raise();
                window.activateWindow();
            });
        }
    });
}

// Clicking anywhere on a slider jumps there instead of paging.
class AppStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget, QStyleHintReturn *data) const override
    {
        if (hint == SH_Slider_AbsoluteSetButtons)
            return Qt::LeftButton;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
};

void applyTheme(QApplication &app)
{
    app.setStyle(new AppStyle(QStyleFactory::create(QStringLiteral("Fusion"))));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x14, 0x14, 0x17));
    palette.setColor(QPalette::WindowText, QColor(0xd8, 0xd8, 0xdc));
    palette.setColor(QPalette::Base, QColor(0x18, 0x18, 0x1c));
    palette.setColor(QPalette::AlternateBase, QColor(0x1d, 0x1d, 0x22));
    palette.setColor(QPalette::Text, QColor(0xd8, 0xd8, 0xdc));
    palette.setColor(QPalette::PlaceholderText, QColor(0x77, 0x77, 0x7f));
    palette.setColor(QPalette::Button, QColor(0x22, 0x22, 0x28));
    palette.setColor(QPalette::ButtonText, QColor(0xd8, 0xd8, 0xdc));
    palette.setColor(QPalette::Highlight, QColor(0x2b, 0x4a, 0x70));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor(0x20, 0x20, 0x26));
    palette.setColor(QPalette::ToolTipText, QColor(0xee, 0xee, 0xee));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x5a, 0x5a, 0x62));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x5a, 0x5a, 0x62));
    app.setPalette(palette);

    app.setStyleSheet(QStringLiteral(R"(
        #ControlBar { background: #1c1c21; border-top: 1px solid #2a2a30; }
        #ControlBar QToolButton { border: none; padding: 4px; border-radius: 4px; }
        #ControlBar QToolButton:hover { background: #2a2a31; }
        #ControlBar QToolButton::menu-indicator { image: none; width: 0; }
        #Time { color: #b8b8be; padding-left: 4px; }

        QSlider#Seek::groove:horizontal { height: 4px; background: #3a3a42; border-radius: 2px; }
        QSlider#Seek::sub-page:horizontal { background: #4aa3ff; border-radius: 2px; }
        QSlider#Seek::handle:horizontal { background: #ffffff; width: 12px; margin: -4px 0; border-radius: 6px; }
        QSlider#Volume::groove:horizontal { height: 3px; background: #3a3a42; border-radius: 1px; }
        QSlider#Volume::sub-page:horizontal { background: #d8d8dc; border-radius: 1px; }
        QSlider#Volume::handle:horizontal { background: #ffffff; width: 10px; margin: -4px 0; border-radius: 5px; }

        #Playlist { background: #18181c; }
        #PanelTitle { color: #8a8a92; font-weight: 600; letter-spacing: 1px; }
        #PanelCount { color: #6a6a72; padding-right: 8px; }
        #Playlist QLineEdit { background: #222228; border: 1px solid #2e2e35; border-radius: 4px; padding: 4px 6px; }
        #Playlist QLineEdit:focus { border-color: #4aa3ff; }
        #Playlist QTreeWidget { background: #18181c; border: none; outline: 0; }
        #Playlist QTreeWidget::item { height: 24px; padding-left: 2px; }
        #Playlist QTreeWidget::item:hover { background: #222228; }
        #Playlist QTreeWidget::item:selected { background: #2b3a4f; color: #ffffff; }
        #Playlist QHeaderView::section { background: #18181c; color: #6a6a72; border: none;
                                         border-bottom: 1px solid #2a2a30; padding: 4px; }

        QSplitter::handle { background: #2a2a30; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
        QScrollBar::handle:vertical { background: #3a3a42; border-radius: 4px; min-height: 30px; margin: 2px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }

        QMenu { background: #202026; border: 1px solid #33333a; padding: 4px; }
        QMenu::item { padding: 5px 24px 5px 22px; border-radius: 3px; }
        QMenu::item:selected { background: #2b3a4f; }
        QMenu::item:disabled { color: #5a5a62; }
        QMenu::separator { height: 1px; background: #33333a; margin: 4px 6px; }
        QToolTip { background: #202026; color: #eeeeee; border: 1px solid #33333a; padding: 3px; }
    )"));
}

} // namespace

int main(int argc, char *argv[])
{
    if (!preferNativeVideo()) {
        // macOS only exposes modern OpenGL through a core profile, which mpv needs.
        QSurfaceFormat format;
        format.setVersion(3, 2);
        format.setProfile(QSurfaceFormat::CoreProfile);
        QSurfaceFormat::setDefaultFormat(format);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Glide"));
    QApplication::setOrganizationName(QStringLiteral("Glide"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/assets/icon-256.png")));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    // libmpv parses numbers with the C locale; Qt sets LC_NUMERIC from the system.
    std::setlocale(LC_NUMERIC, "C");

    QStringList files;
    for (const QString &arg : app.arguments().mid(1)) {
        if (!arg.startsWith('-'))
            files << QFileInfo(arg).absoluteFilePath();
    }
    if (forwardToRunningInstance(files))
        return 0;

    applyTheme(app);
    MainWindow window;
    window.show();

    QLocalServer server;
    listenForOtherInstances(server, window);

    if (!files.isEmpty())
        window.openPaths(files);
    return app.exec();
}

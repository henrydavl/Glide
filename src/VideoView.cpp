#include "VideoView.h"

#include "MpvPlayer.h"

#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>

#include <mpv/render_gl.h>

namespace {

// mpv creates a child window inside this widget and presents into it itself, so Qt never
// touches the video frames. On Windows that means D3D11 decode → deinterlace → present.
class NativeVideoView final : public QWidget, public VideoView {
public:
    explicit NativeVideoView(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_DontCreateNativeAncestors);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_PaintOnScreen);
        setMouseTracking(true);
        setFocusPolicy(Qt::ClickFocus);
    }

    QWidget *widget() override { return this; }
    QPaintEngine *paintEngine() const override { return nullptr; }
    QString backendName() const override { return QStringLiteral("native"); }

    void attach(MpvPlayer *player) override
    {
        int64_t wid = int64_t(winId());
        mpv_set_option(player->handle(), "wid", MPV_FORMAT_INT64, &wid);
        player->setOption("vo", QStringLiteral("gpu-next"));
        player->setOption("force-window", QStringLiteral("yes"));
        // Let mouse events fall through mpv's window to this widget, and keep file drops
        // for our playlist instead of mpv's own handler.
        player->setOption("input-cursor-passthrough", QStringLiteral("yes"));
        player->setOption("drag-and-drop", QStringLiteral("no"));
#ifdef Q_OS_WIN
        player->setOption("gpu-api", QStringLiteral("d3d11"));
        // mpv knows the real refresh rate here, so resample audio to the display clock
        // instead of dropping/repeating frames (the classic 25/50 fps on 60 Hz judder).
        player->setOption("video-sync", QStringLiteral("display-resample"));
#endif
    }

protected:
    void paintEvent(QPaintEvent *) override {}
};

void *getProcAddress(void *, const char *name)
{
    QOpenGLContext *context = QOpenGLContext::currentContext();
    return context ? reinterpret_cast<void *>(context->getProcAddress(name)) : nullptr;
}

class GLVideoView final : public QOpenGLWidget, public VideoView {
public:
    explicit GLVideoView(QWidget *parent)
        : QOpenGLWidget(parent)
    {
        setMouseTracking(true);
        setFocusPolicy(Qt::ClickFocus);
        connect(this, &QOpenGLWidget::frameSwapped, this, [this] {
            if (m_context)
                mpv_render_context_report_swap(m_context);
        });
    }

    ~GLVideoView() override { detach(); }

    QWidget *widget() override { return this; }
    QString backendName() const override { return QStringLiteral("opengl"); }

    void attach(MpvPlayer *player) override
    {
        m_player = player;
        player->setOption("vo", QStringLiteral("libmpv"));
    }

    void detach() override
    {
        if (!m_context)
            return;
        makeCurrent();
        mpv_render_context_free(m_context);
        m_context = nullptr;
        doneCurrent();
    }

protected:
    void initializeGL() override
    {
        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] { detach(); }, Qt::DirectConnection);

        mpv_opengl_init_params glParams{getProcAddress, nullptr};
        int advancedControl = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams},
            {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advancedControl},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        if (mpv_render_context_create(&m_context, m_player->handle(), params) < 0) {
            qWarning("mpv: failed to create the OpenGL render context");
            m_context = nullptr;
            return;
        }
        mpv_render_context_set_update_callback(m_context, [](void *self) {
            auto *view = static_cast<GLVideoView *>(self);
            QMetaObject::invokeMethod(view, [view] { view->handleUpdate(); }, Qt::QueuedConnection);
        }, this);
    }

    void paintGL() override
    {
        if (!m_context) {
            QOpenGLFunctions *gl = context()->functions();
            gl->glClearColor(0, 0, 0, 1);
            gl->glClear(GL_COLOR_BUFFER_BIT);
            return;
        }
        const qreal ratio = devicePixelRatioF();
        mpv_opengl_fbo fbo{int(defaultFramebufferObject()), int(width() * ratio), int(height() * ratio), 0};
        int flipY = 1;
        // Never let mpv sleep on the GUI thread waiting for a frame's display time.
        int blockForTargetTime = 0;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &blockForTargetTime},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        mpv_render_context_render(m_context, params);
    }

private:
    void handleUpdate()
    {
        // With advanced control, mpv_render_context_update() must answer every callback.
        if (m_context && (mpv_render_context_update(m_context) & MPV_RENDER_UPDATE_FRAME))
            update();
    }

    MpvPlayer *m_player = nullptr;
    mpv_render_context *m_context = nullptr;
};

} // namespace

bool preferNativeVideo()
{
    const QByteArray forced = qgetenv("GLIDE_VIDEO");
    if (forced == "native")
        return true;
    if (forced == "gl")
        return false;
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

VideoView *createVideoView(QWidget *parent)
{
    if (preferNativeVideo())
        return new NativeVideoView(parent);
    return new GLVideoView(parent);
}

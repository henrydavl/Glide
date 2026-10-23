#include "DurationProber.h"

#include <QMetaObject>
#include <QTimer>

DurationProber::DurationProber(QObject *parent)
    : QObject(parent)
    , m_mpv(mpv_create())
{
    if (!m_mpv)
        return;
    const char *options[][2] = {
        // Tracks stay selected (mpv aborts a file with none), but output goes nowhere and
        // the file is replaced the moment its duration is known.
        {"vo", "null"}, {"ao", "null"}, {"sid", "no"}, {"hwdec", "no"}, {"pause", "yes"}, {"idle", "yes"}, {"cache", "no"}, {"ytdl", "no"}, {"audio-display", "no"},
        {"load-stats-overlay", "no"}, {"load-osd-console", "no"}, {"load-auto-profiles", "no"},
        {"load-select", "no"}, {"load-positioning", "no"}, {"load-commands", "no"},
    };
    for (const auto &option : options)
        mpv_set_option_string(m_mpv, option[0], option[1]); // unknown builtin-script options are harmless
    if (mpv_initialize(m_mpv) < 0) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }
    mpv_set_wakeup_callback(m_mpv, [](void *self) {
        QMetaObject::invokeMethod(static_cast<DurationProber *>(self), &DurationProber::processEvents, Qt::QueuedConnection);
    }, this);
}

DurationProber::~DurationProber()
{
    if (!m_mpv)
        return;
    mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
    mpv_terminate_destroy(m_mpv);
}

void DurationProber::enqueue(const QStringList &paths)
{
    if (!m_mpv)
        return;
    for (const QString &path : paths) {
        if (path != m_current && !m_queue.contains(path))
            m_queue.append(path);
    }
    // Give the file the user just opened a head start on the disk before probing the rest.
    QTimer::singleShot(1500, this, &DurationProber::next);
}

void DurationProber::clear()
{
    m_queue.clear();
}

void DurationProber::next()
{
    if (!m_current.isEmpty())
        return;
    if (m_queue.isEmpty()) {
        const char *stop[] = {"stop", nullptr};
        mpv_command_async(m_mpv, 0, stop);
        return;
    }
    m_current = m_queue.takeFirst();
    const QByteArray path = m_current.toUtf8();
    const char *args[] = {"loadfile", path.constData(), "replace", nullptr};
    mpv_command_async(m_mpv, 0, args);
}

void DurationProber::finish(double seconds)
{
    if (m_current.isEmpty())
        return;
    const QString path = m_current;
    m_current.clear();
    emit probed(path, seconds);
    QTimer::singleShot(50, this, &DurationProber::next);
}

void DurationProber::processEvents()
{
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;
        if (event->event_id == MPV_EVENT_FILE_LOADED) {
            double seconds = 0;
            if (mpv_get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &seconds) < 0 || seconds <= 0)
                seconds = -1;
            finish(seconds);
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            const auto *end = static_cast<mpv_event_end_file *>(event->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR)
                finish(-1);
        }
    }
}

#include "MpvPlayer.h"

#include <QDebug>
#include <QMetaObject>
#include <QVariant>
#include <QVector>

#include <cmath>

namespace {

QVariant nodeToVariant(const mpv_node *node)
{
    switch (node->format) {
    case MPV_FORMAT_STRING:
        return QString::fromUtf8(node->u.string);
    case MPV_FORMAT_FLAG:
        return bool(node->u.flag);
    case MPV_FORMAT_INT64:
        return qlonglong(node->u.int64);
    case MPV_FORMAT_DOUBLE:
        return node->u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList list;
        for (int i = 0; i < node->u.list->num; ++i)
            list.append(nodeToVariant(&node->u.list->values[i]));
        return list;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap map;
        for (int i = 0; i < node->u.list->num; ++i)
            map.insert(QString::fromUtf8(node->u.list->keys[i]), nodeToVariant(&node->u.list->values[i]));
        return map;
    }
    default:
        return {};
    }
}

} // namespace

QString MpvTrack::label() const
{
    QStringList parts;
    if (!title.isEmpty())
        parts << title;
    if (!lang.isEmpty())
        parts << QStringLiteral("[%1]").arg(lang);
    QString details = codec;
    if (channels > 0)
        details += QStringLiteral(" %1ch").arg(channels);
    if (external)
        details += QStringLiteral(" external");
    if (!details.trimmed().isEmpty())
        parts << QStringLiteral("(%1)").arg(details.trimmed());
    return QStringLiteral("%1: %2").arg(id).arg(parts.isEmpty() ? QStringLiteral("Track") : parts.join(' '));
}

MpvPlayer::MpvPlayer(QObject *parent)
    : QObject(parent)
    , m_mpv(mpv_create())
{
    if (!m_mpv)
        qFatal("mpv_create() failed");
    mpv_set_wakeup_callback(m_mpv, [](void *self) {
        QMetaObject::invokeMethod(static_cast<MpvPlayer *>(self), &MpvPlayer::processEvents, Qt::QueuedConnection);
    }, this);
}

MpvPlayer::~MpvPlayer()
{
    mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
    mpv_terminate_destroy(m_mpv);
}

void MpvPlayer::setOption(const char *name, const QString &value)
{
    if (mpv_set_option_string(m_mpv, name, value.toUtf8().constData()) < 0)
        qWarning() << "mpv: could not set option" << name << "=" << value;
}

void MpvPlayer::initialize()
{
    if (mpv_initialize(m_mpv) < 0)
        qFatal("mpv_initialize() failed");
    mpv_request_log_messages(m_mpv, "warn");

    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "track-list", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
}

void MpvPlayer::loadFile(const QString &path, double startAt)
{
    QStringList args{QStringLiteral("loadfile"), path, QStringLiteral("replace")};
    if (startAt > 0)
        args << QStringLiteral("-1") << QStringLiteral("start=%1").arg(startAt, 0, 'f', 2);
    command(args);
}

namespace {

// mpv wants a NULL-terminated char* array; the QByteArrays keep the strings alive.
struct CommandArgs {
    explicit CommandArgs(const QStringList &args)
    {
        utf8.reserve(args.size());
        for (const QString &arg : args)
            utf8.append(arg.toUtf8());
        for (const QByteArray &arg : utf8)
            argv.append(arg.constData());
        argv.append(nullptr);
    }
    QList<QByteArray> utf8;
    QVector<const char *> argv;
};

} // namespace

void MpvPlayer::command(const QStringList &args)
{
    CommandArgs command(args);
    mpv_command_async(m_mpv, 0, command.argv.data());
}

bool MpvPlayer::commandSync(const QStringList &args)
{
    CommandArgs command(args);
    return mpv_command(m_mpv, command.argv.data()) >= 0;
}

void MpvPlayer::setPaused(bool paused)
{
    int flag = paused;
    mpv_set_property_async(m_mpv, 0, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvPlayer::setVolume(double volume)
{
    mpv_set_property_async(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE, &volume);
}

void MpvPlayer::seekTo(double seconds, bool exact)
{
    // Keyframe seeks while dragging stay instant even on huge .ts files; the exact seek
    // on release lands on the precise frame.
    command({QStringLiteral("seek"), QString::number(seconds, 'f', 3),
             exact ? QStringLiteral("absolute+exact") : QStringLiteral("absolute+keyframes")});
}

void MpvPlayer::showText(const QString &text, int durationMs)
{
    command({QStringLiteral("show-text"), text, QString::number(durationMs)});
}

QString MpvPlayer::propertyString(const char *name) const
{
    char *value = mpv_get_property_string(m_mpv, name);
    const QString result = value ? QString::fromUtf8(value) : QString();
    mpv_free(value);
    return result;
}

QVariant MpvPlayer::propertyValue(const char *name) const
{
    mpv_node node;
    if (mpv_get_property(m_mpv, name, MPV_FORMAT_NODE, &node) < 0)
        return {};
    const QVariant value = nodeToVariant(&node);
    mpv_free_node_contents(&node);
    return value;
}

void MpvPlayer::setPropertyString(const char *name, const QString &value)
{
    if (mpv_set_property_string(m_mpv, name, value.toUtf8().constData()) < 0)
        qWarning() << "mpv: could not set property" << name << "=" << value;
}

void MpvPlayer::processEvents()
{
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;
        switch (event->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE:
            handleProperty(static_cast<mpv_event_property *>(event->data));
            break;
        case MPV_EVENT_FILE_LOADED:
            emit fileLoaded();
            break;
        case MPV_EVENT_END_FILE: {
            const auto *end = static_cast<mpv_event_end_file *>(event->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR)
                emit loadFailed(QString::fromUtf8(mpv_error_string(end->error)));
            break;
        }
        case MPV_EVENT_LOG_MESSAGE: {
            const auto *msg = static_cast<mpv_event_log_message *>(event->data);
            qWarning().noquote() << QStringLiteral("[mpv/%1]").arg(QString::fromUtf8(msg->prefix))
                                 << QString::fromUtf8(msg->text).trimmed();
            break;
        }
        default:
            break;
        }
    }
}

void MpvPlayer::handleProperty(const mpv_event_property *prop)
{
    const QByteArray name(prop->name);
    const bool has = prop->format != MPV_FORMAT_NONE && prop->data;

    if (name == "time-pos") {
        m_position = has ? *static_cast<double *>(prop->data) : 0;
        // time-pos fires every frame; the UI only needs a few updates per second.
        if (std::abs(m_position - m_emittedPosition) >= 0.2 || m_position == 0) {
            m_emittedPosition = m_position;
            emit positionChanged(m_position);
        }
    } else if (name == "duration") {
        m_duration = has ? *static_cast<double *>(prop->data) : 0;
        emit durationChanged(m_duration);
    } else if (name == "pause") {
        m_paused = has && *static_cast<int *>(prop->data);
        emit pausedChanged(m_paused);
    } else if (name == "volume") {
        if (has) {
            m_volume = *static_cast<double *>(prop->data);
            emit volumeChanged(m_volume);
        }
    } else if (name == "mute") {
        m_muted = has && *static_cast<int *>(prop->data);
        emit mutedChanged(m_muted);
    } else if (name == "track-list") {
        m_tracks.clear();
        if (has) {
            const QVariantList list = nodeToVariant(static_cast<mpv_node *>(prop->data)).toList();
            for (const QVariant &entry : list) {
                const QVariantMap map = entry.toMap();
                MpvTrack track;
                track.id = map.value(QStringLiteral("id")).toLongLong();
                track.type = map.value(QStringLiteral("type")).toString();
                track.title = map.value(QStringLiteral("title")).toString();
                track.lang = map.value(QStringLiteral("lang")).toString();
                track.codec = map.value(QStringLiteral("codec")).toString();
                track.channels = map.value(QStringLiteral("demux-channel-count")).toInt();
                track.selected = map.value(QStringLiteral("selected")).toBool();
                track.external = map.value(QStringLiteral("external")).toBool();
                m_tracks.append(track);
            }
        }
        emit tracksChanged();
    } else if (name == "eof-reached") {
        if (has && *static_cast<int *>(prop->data))
            emit endReached();
    }
}

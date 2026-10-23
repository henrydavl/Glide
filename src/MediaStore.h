#pragma once

#include <QHash>
#include <QSettings>
#include <QString>

// Per-file memory: probed duration and resume position. Keyed on path + size + mtime so
// a replaced file doesn't inherit stale data.
class MediaStore {
public:
    MediaStore();

    // 0 = never probed, -1 = probe failed.
    double duration(const QString &path);
    void setDuration(const QString &path, double seconds);

    // 0 = start from the beginning.
    double resumePosition(const QString &path);
    void setResumePosition(const QString &path, double seconds);

private:
    QString key(const QString &path);

    QSettings m_settings;
    QHash<QString, QString> m_keys;
};

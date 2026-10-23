#include "MediaStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>

MediaStore::MediaStore()
    : m_settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Glide"), QStringLiteral("media"))
{
}

double MediaStore::duration(const QString &path)
{
    return m_settings.value(key(path) + QStringLiteral("/duration"), 0.0).toDouble();
}

void MediaStore::setDuration(const QString &path, double seconds)
{
    m_settings.setValue(key(path) + QStringLiteral("/duration"), seconds);
}

double MediaStore::resumePosition(const QString &path)
{
    return m_settings.value(key(path) + QStringLiteral("/resume"), 0.0).toDouble();
}

void MediaStore::setResumePosition(const QString &path, double seconds)
{
    const QString name = key(path) + QStringLiteral("/resume");
    if (seconds > 0)
        m_settings.setValue(name, seconds);
    else
        m_settings.remove(name);
}

QString MediaStore::key(const QString &path)
{
    auto cached = m_keys.constFind(path);
    if (cached != m_keys.constEnd())
        return *cached;
    const QFileInfo info(path);
    const QByteArray identity = path.toUtf8() + '|' + QByteArray::number(info.size()) + '|'
        + QByteArray::number(info.lastModified().toSecsSinceEpoch());
    const QString hashed = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Md5).toHex());
    m_keys.insert(path, hashed);
    return hashed;
}

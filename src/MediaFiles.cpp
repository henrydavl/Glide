#include "MediaFiles.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include <algorithm>
#include <cmath>

namespace {

const QStringList &videoExtensions()
{
    static const QStringList extensions = {
        "ts", "m2ts", "mts", "m2t", "tp", "trp", "tod", "mp4", "m4v", "mkv", "webm", "avi", "mov",
        "wmv", "asf", "flv", "f4v", "mpg", "mpeg", "mpe", "m1v", "m2v", "vob", "3gp", "3g2", "ogv",
        "ogm", "rm", "rmvb", "divx", "wtv", "dvr-ms", "mxf", "dv", "evo", "y4m", "h264", "264",
        "hevc", "265",
    };
    return extensions;
}

const QStringList &subtitleExtensions()
{
    static const QStringList extensions = {"srt", "ass", "ssa", "sub", "idx", "vtt", "sup", "smi"};
    return extensions;
}

} // namespace

bool isVideoFile(const QString &path)
{
    return videoExtensions().contains(QFileInfo(path).suffix().toLower());
}

bool isSubtitleFile(const QString &path)
{
    return subtitleExtensions().contains(QFileInfo(path).suffix().toLower());
}

QString videoFilePatterns()
{
    QStringList patterns;
    for (const QString &ext : videoExtensions())
        patterns << QStringLiteral("*.") + ext;
    return patterns.join(' ');
}

QStringList videosInFolder(const QString &folder)
{
    QStringList videos;
    const QFileInfoList entries = QDir(folder).entryInfoList(QDir::Files | QDir::Readable);
    for (const QFileInfo &entry : entries) {
        if (isVideoFile(entry.fileName()))
            videos << entry.absoluteFilePath();
    }
    naturalSort(videos);
    return videos;
}

void naturalSort(QStringList &paths)
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(paths.begin(), paths.end(), [&](const QString &a, const QString &b) {
        return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
    });
}

void revealInFolder(const QString &path)
{
#if defined(Q_OS_WIN)
    QProcess process;
    process.setProgram(QStringLiteral("explorer.exe"));
    process.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(path)));
    process.startDetached();
#elif defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path});
#else
    QProcess::startDetached(QStringLiteral("xdg-open"), {QFileInfo(path).absolutePath()});
#endif
}

QString formatTime(double seconds, bool withHours)
{
    const qint64 total = qint64(std::max(0.0, std::floor(seconds)));
    const qint64 h = total / 3600;
    const qint64 m = (total / 60) % 60;
    const qint64 s = total % 60;
    if (h > 0 || withHours)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QStringLiteral("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}

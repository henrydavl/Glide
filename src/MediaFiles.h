#pragma once

#include <QString>
#include <QStringList>

bool isVideoFile(const QString &path);
bool isSubtitleFile(const QString &path);
// "*.ts *.mkv ..." for file dialogs.
QString videoFilePatterns();
// Video files directly inside a folder, in natural order ("Ep 2" before "Ep 10").
QStringList videosInFolder(const QString &folder);
void naturalSort(QStringList &paths);
void revealInFolder(const QString &path);
// 75 → "01:15", 3725 → "1:02:05".
QString formatTime(double seconds, bool withHours = false);

#pragma once

#include <QObject>
#include <QStringList>

#include <mpv/client.h>

// Fills in playlist lengths, PotPlayer-style, using a second headless mpv instance that
// opens one file at a time (no decoding) and reads its duration.
class DurationProber : public QObject {
    Q_OBJECT
public:
    explicit DurationProber(QObject *parent = nullptr);
    ~DurationProber() override;

    void enqueue(const QStringList &paths);
    void clear();

signals:
    // seconds is -1 when the file could not be opened.
    void probed(const QString &path, double seconds);

private:
    void next();
    void finish(double seconds);
    void processEvents();

    mpv_handle *m_mpv = nullptr;
    QStringList m_queue;
    QString m_current;
};

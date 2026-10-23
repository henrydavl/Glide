#pragma once

#include <QWidget>

class MediaStore;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

// PotPlayer-style side panel listing every video in the folder, with lengths and
// resume points.
class PlaylistPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistPanel(MediaStore *store, QWidget *parent = nullptr);

    void setFiles(const QStringList &paths);
    void addFiles(const QStringList &paths);
    QStringList files() const;

    void setCurrent(const QString &path);
    // The file `step` entries after (or before, if negative) the current one, or empty.
    QString neighbour(int step) const;
    // Redraws the length / resume column for one file.
    void refresh(const QString &path);

    QTreeWidget *view() const { return m_tree; }

signals:
    void playRequested(const QString &path);
    void addRequested();

private:
    QTreeWidgetItem *itemFor(const QString &path) const;
    QTreeWidgetItem *makeItem(const QString &path);
    void updateItem(QTreeWidgetItem *item);
    void updateCount();
    void applyFilter();
    void removeSelected();
    void showMenu(const QPoint &pos);

    MediaStore *m_store;
    QLabel *m_count;
    QLineEdit *m_filter;
    QTreeWidget *m_tree;
    QString m_current;
};

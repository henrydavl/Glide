#include "PlaylistPanel.h"

#include "MediaFiles.h"
#include "MediaStore.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QShortcut>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int PathRole = Qt::UserRole;
const QColor CurrentColor(0x5c, 0xb0, 0xff);

} // namespace

PlaylistPanel::PlaylistPanel(MediaStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
    , m_count(new QLabel(this))
    , m_filter(new QLineEdit(this))
    , m_tree(new QTreeWidget(this))
{
    setObjectName(QStringLiteral("Playlist"));
    setAttribute(Qt::WA_StyledBackground);
    setMinimumWidth(220);

    auto *title = new QLabel(tr("PLAYLIST"), this);
    title->setObjectName(QStringLiteral("PanelTitle"));
    m_count->setObjectName(QStringLiteral("PanelCount"));
    m_filter->setPlaceholderText(tr("Search playlist"));
    m_filter->setClearButtonEnabled(true);
    // Only a deliberate click focuses search, so keys at startup reach the player.
    m_filter->setFocusPolicy(Qt::ClickFocus);
    m_tree->setFocusPolicy(Qt::ClickFocus);

    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Name"), tr("Length")});
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    // Episode numbers usually sit at the end of the name, so elide the middle.
    m_tree->setTextElideMode(Qt::ElideMiddle);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(m_count);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 0, 0);
    layout->setSpacing(6);
    layout->addLayout(header);
    auto *filterRow = new QHBoxLayout;
    filterRow->setContentsMargins(0, 0, 8, 0);
    filterRow->addWidget(m_filter);
    layout->addLayout(filterRow);
    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        emit playRequested(item->data(0, PathRole).toString());
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &PlaylistPanel::showMenu);
    connect(m_filter, &QLineEdit::textChanged, this, &PlaylistPanel::applyFilter);
    for (const QKeySequence &key : {QKeySequence(QKeySequence::Delete), QKeySequence(Qt::Key_Backspace)})
        connect(new QShortcut(key, m_tree, nullptr, nullptr, Qt::WidgetShortcut), &QShortcut::activated, this, &PlaylistPanel::removeSelected);

    updateCount();
}

void PlaylistPanel::setFiles(const QStringList &paths)
{
    m_tree->clear();
    addFiles(paths);
}

void PlaylistPanel::addFiles(const QStringList &paths)
{
    for (const QString &path : paths) {
        if (!itemFor(path))
            m_tree->addTopLevelItem(makeItem(path));
    }
    applyFilter();
    updateCount();
    if (QTreeWidgetItem *current = itemFor(m_current))
        m_tree->scrollToItem(current, QAbstractItemView::PositionAtCenter);
}

QStringList PlaylistPanel::files() const
{
    QStringList paths;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        paths << m_tree->topLevelItem(i)->data(0, PathRole).toString();
    return paths;
}

void PlaylistPanel::setCurrent(const QString &path)
{
    const QString previous = m_current;
    m_current = path;
    if (QTreeWidgetItem *item = itemFor(previous))
        updateItem(item);
    if (QTreeWidgetItem *item = itemFor(path)) {
        updateItem(item);
        m_tree->setCurrentItem(item);
        m_tree->scrollToItem(item);
    }
}

QString PlaylistPanel::neighbour(int step) const
{
    const int count = m_tree->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        if (m_tree->topLevelItem(i)->data(0, PathRole).toString() != m_current)
            continue;
        const int target = i + step;
        if (target < 0 || target >= count)
            return {};
        return m_tree->topLevelItem(target)->data(0, PathRole).toString();
    }
    return {};
}

void PlaylistPanel::refresh(const QString &path)
{
    if (QTreeWidgetItem *item = itemFor(path))
        updateItem(item);
}

QTreeWidgetItem *PlaylistPanel::itemFor(const QString &path) const
{
    if (path.isEmpty())
        return nullptr;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->data(0, PathRole).toString() == path)
            return item;
    }
    return nullptr;
}

QTreeWidgetItem *PlaylistPanel::makeItem(const QString &path)
{
    auto *item = new QTreeWidgetItem;
    item->setData(0, PathRole, path);
    item->setText(0, QFileInfo(path).fileName());
    item->setToolTip(0, QDir::toNativeSeparators(path));
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    // Not drop-enabled, so dragging to reorder never nests one file under another.
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled);
    updateItem(item);
    return item;
}

void PlaylistPanel::updateItem(QTreeWidgetItem *item)
{
    const QString path = item->data(0, PathRole).toString();
    const double duration = m_store->duration(path);
    const double resume = m_store->resumePosition(path);
    const bool hours = duration >= 3600;
    QString length;
    if (duration > 0 && resume > 0)
        length = QStringLiteral("%1 / %2").arg(formatTime(resume, hours), formatTime(duration, hours));
    else if (duration > 0)
        length = formatTime(duration);
    item->setText(1, length);

    const bool current = path == m_current;
    QFont font = m_tree->font();
    font.setBold(current);
    for (int column = 0; column < 2; ++column) {
        item->setFont(column, font);
        item->setForeground(column, current ? QBrush(CurrentColor) : QBrush());
    }
}

void PlaylistPanel::updateCount()
{
    const int count = m_tree->topLevelItemCount();
    m_count->setText(count == 1 ? tr("1 file") : tr("%1 files").arg(count));
}

void PlaylistPanel::applyFilter()
{
    const QString text = m_filter->text().trimmed();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        item->setHidden(!text.isEmpty() && !item->text(0).contains(text, Qt::CaseInsensitive));
    }
}

void PlaylistPanel::removeSelected()
{
    qDeleteAll(m_tree->selectedItems());
    updateCount();
}

void PlaylistPanel::showMenu(const QPoint &pos)
{
    QMenu menu(this);
    if (QTreeWidgetItem *item = m_tree->itemAt(pos)) {
        const QString path = item->data(0, PathRole).toString();
        menu.addAction(tr("Play"), this, [this, path] { emit playRequested(path); });
        menu.addAction(tr("Show in folder"), this, [path] { revealInFolder(path); });
        menu.addAction(tr("Remove from playlist"), this, &PlaylistPanel::removeSelected);
        menu.addSeparator();
    }
    menu.addAction(tr("Add files…"), this, &PlaylistPanel::addRequested);
    menu.addAction(tr("Sort by name"), this, [this] {
        QStringList paths = files();
        naturalSort(paths);
        setFiles(paths);
    });
    menu.addAction(tr("Clear playlist"), this, [this] {
        m_tree->clear();
        updateCount();
    });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

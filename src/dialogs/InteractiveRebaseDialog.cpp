/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "InteractiveRebaseDialog.h"

#include <QColor>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTableView>
#include <QTextStream>
#include <QVBoxLayout>

// --- InteractiveRebaseModel ---

InteractiveRebaseModel::InteractiveRebaseModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void InteractiveRebaseModel::setEntries(const QVector<RebaseEntry> &entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

int InteractiveRebaseModel::rowCount(const QModelIndex &) const
{
    return m_entries.size();
}

int InteractiveRebaseModel::columnCount(const QModelIndex &) const
{
    return 4;
}

QVariant InteractiveRebaseModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size()) return {};

    const auto &e = m_entries[index.row()];

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case 0: return actionToString(e.action);
        case 1: return e.hash.left(7);
        case 2: return e.subject;
        case 3: return e.author;
        default: break;
        }
    }

    if (role == Qt::ForegroundRole && e.action == RebaseEntry::Drop) {
        return QColor(Qt::gray);
    }

    return {};
}

QVariant InteractiveRebaseModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case 0: return tr("Action");
    case 1: return tr("SHA");
    case 2: return tr("Message");
    case 3: return tr("Author");
    default: break;
    }
    return {};
}

Qt::ItemFlags InteractiveRebaseModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.column() == 0)
        f |= Qt::ItemIsEditable;
    f |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    return f;
}

bool InteractiveRebaseModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.column() != 0) return false;
    m_entries[index.row()].action = stringToAction(value.toString());
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::ForegroundRole});
    return true;
}

bool InteractiveRebaseModel::moveRows(const QModelIndex &, int sourceRow, int count,
                                      const QModelIndex &, int destinationChild)
{
    if (count != 1) return false;
    if (sourceRow == destinationChild || sourceRow < 0 || sourceRow >= m_entries.size()) return false;

    int dest = destinationChild > sourceRow ? destinationChild + 1 : destinationChild;
    beginMoveRows({}, sourceRow, sourceRow, {}, dest);
    m_entries.move(sourceRow, destinationChild);
    endMoveRows();
    return true;
}

QString InteractiveRebaseModel::actionToString(RebaseEntry::Action a)
{
    switch (a) {
    case RebaseEntry::Pick:   return QStringLiteral("pick");
    case RebaseEntry::Reword: return QStringLiteral("reword");
    case RebaseEntry::Edit:   return QStringLiteral("edit");
    case RebaseEntry::Squash: return QStringLiteral("squash");
    case RebaseEntry::Fixup:  return QStringLiteral("fixup");
    case RebaseEntry::Drop:   return QStringLiteral("drop");
    }
    return QStringLiteral("pick");
}

RebaseEntry::Action InteractiveRebaseModel::stringToAction(const QString &s)
{
    if (s == QLatin1String("pick") || s == QLatin1String("p"))     return RebaseEntry::Pick;
    if (s == QLatin1String("reword") || s == QLatin1String("r"))   return RebaseEntry::Reword;
    if (s == QLatin1String("edit") || s == QLatin1String("e"))     return RebaseEntry::Edit;
    if (s == QLatin1String("squash") || s == QLatin1String("s"))   return RebaseEntry::Squash;
    if (s == QLatin1String("fixup") || s == QLatin1String("f"))    return RebaseEntry::Fixup;
    if (s == QLatin1String("drop") || s == QLatin1String("d"))     return RebaseEntry::Drop;
    return RebaseEntry::Pick;
}

// --- InteractiveRebaseDialog ---

InteractiveRebaseDialog::InteractiveRebaseDialog(const QString &todoFilePath, QWidget *parent)
    : QDialog(parent)
    , m_todoPath(todoFilePath)
{
    setWindowTitle(tr("Interactive Rebase"));
    setMinimumSize(700, 400);
    resize(800, 500);

    auto *layout = new QVBoxLayout(this);

    m_model = new InteractiveRebaseModel(this);
    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setDragEnabled(true);
    m_table->setAcceptDrops(true);
    m_table->setDragDropMode(QAbstractItemView::InternalMove);
    m_table->setDropIndicatorShown(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    auto *btnRow = new QHBoxLayout;
    m_moveUpBtn = new QPushButton(tr("Move Up"), this);
    m_moveDownBtn = new QPushButton(tr("Move Down"), this);
    btnRow->addWidget(m_moveUpBtn);
    btnRow->addWidget(m_moveDownBtn);
    btnRow->addStretch();
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_startBtn = new QPushButton(tr("Start Rebase"), this);
    m_startBtn->setDefault(true);
    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(m_startBtn);
    layout->addLayout(btnRow);

    connect(m_moveUpBtn, &QPushButton::clicked, this, &InteractiveRebaseDialog::onMoveUp);
    connect(m_moveDownBtn, &QPushButton::clicked, this, &InteractiveRebaseDialog::onMoveDown);
    connect(m_startBtn, &QPushButton::clicked, this, &InteractiveRebaseDialog::onStartRebase);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    parseTodoFile(todoFilePath);
}

void InteractiveRebaseDialog::onMoveUp()
{
    auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    int row = rows.first().row();
    if (row <= 0) return;
    m_model->moveRows({}, row, 1, {}, row - 1);
    m_table->selectRow(row - 1);
}

void InteractiveRebaseDialog::onMoveDown()
{
    auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    int row = rows.first().row();
    if (row >= m_model->rowCount() - 1) return;
    m_model->moveRows({}, row, 1, {}, row + 1);
    m_table->selectRow(row + 1);
}

void InteractiveRebaseDialog::onStartRebase()
{
    writeTodoFile(m_todoPath);
    m_accepted = true;
    accept();
}

void InteractiveRebaseDialog::parseTodoFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QVector<RebaseEntry> entries;
    QTextStream stream(&f);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        if (line.startsWith(QLatin1String("noop"))) continue;

        // Format: <action> <hash> <subject>
        int firstSpace = line.indexOf(QLatin1Char(' '));
        if (firstSpace < 0) continue;
        int secondSpace = line.indexOf(QLatin1Char(' '), firstSpace + 1);
        if (secondSpace < 0) secondSpace = line.size();

        RebaseEntry entry;
        entry.action = InteractiveRebaseModel::stringToAction(line.left(firstSpace));
        entry.hash = line.mid(firstSpace + 1, secondSpace - firstSpace - 1);
        entry.subject = line.mid(secondSpace + 1);
        entries.append(entry);
    }

    m_model->setEntries(entries);
}

void InteractiveRebaseDialog::writeTodoFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return;

    QTextStream stream(&f);
    const auto entries = m_model->entries();
    for (const auto &e : entries) {
        stream << InteractiveRebaseModel::actionToString(e.action)
               << ' ' << e.hash << ' ' << e.subject << '\n';
    }
}

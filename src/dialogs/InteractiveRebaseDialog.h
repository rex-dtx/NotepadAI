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

#ifndef INTERACTIVE_REBASE_DIALOG_H
#define INTERACTIVE_REBASE_DIALOG_H

#include <QAbstractTableModel>
#include <QDialog>
#include <QVector>

#include <cstdint>

class QTableView;
class QPushButton;

struct RebaseEntry {
    enum Action : std::uint8_t { Pick = 0, Reword, Edit, Squash, Fixup, Drop };
    Action action = Pick;
    QString hash;
    QString subject;
    QString author;
};

class InteractiveRebaseModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit InteractiveRebaseModel(QObject *parent = nullptr);

    void setEntries(const QVector<RebaseEntry> &entries);
    QVector<RebaseEntry> entries() const { return m_entries; }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    bool moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                  const QModelIndex &destinationParent, int destinationChild) override;

    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }

    static QString actionToString(RebaseEntry::Action a);
    static RebaseEntry::Action stringToAction(const QString &s);

private:
    QVector<RebaseEntry> m_entries;
};

class InteractiveRebaseDialog : public QDialog
{
    Q_OBJECT
public:
    explicit InteractiveRebaseDialog(const QString &todoFilePath, QWidget *parent = nullptr);

    bool wasAccepted() const { return m_accepted; }

private slots:
    void onMoveUp();
    void onMoveDown();
    void onStartRebase();

private:
    void parseTodoFile(const QString &path);
    void writeTodoFile(const QString &path);

    QTableView *m_table;
    InteractiveRebaseModel *m_model;
    QPushButton *m_moveUpBtn;
    QPushButton *m_moveDownBtn;
    QPushButton *m_startBtn;
    QPushButton *m_cancelBtn;

    QString m_todoPath;
    bool m_accepted = false;
};

#endif // INTERACTIVE_REBASE_DIALOG_H

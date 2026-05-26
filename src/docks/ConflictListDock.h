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

#ifndef CONFLICT_LIST_DOCK_H
#define CONFLICT_LIST_DOCK_H

#include "ConflictEntry.h"
#include "GitOperationManager.h"
#include "GitController.h"

#include <QDockWidget>
#include <QPointer>

class QLabel;
class QPushButton;
class QTreeView;
class QStandardItemModel;
class GitController;

class ConflictListDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit ConflictListDock(GitOperationManager *opMgr, QWidget *parent = nullptr);

    void setController(GitController *controller);
    void setConflicts(const ConflictEntries &entries);
    void setOperationState(GitOperationManager::OperationState state);
    void setDescription(const QString &text);

    static Qt::DockWidgetArea defaultArea() { return Qt::BottomDockWidgetArea; }

signals:
    void openInMergeViewer(const ConflictEntry &entry);
    void continueRequested();
    void abortRequested();
    void skipRequested();
    void acceptOursRequested(const QStringList &relPaths);
    void acceptTheirsRequested(const QStringList &relPaths);

private slots:
    void onDoubleClicked(const QModelIndex &index);
    void onContextMenu(const QPoint &pos);
    void onContinueClicked();
    void onAbortClicked();
    void onSkipClicked();

private:
    void updateTitle();
    void updateButtonStates();
    QStringList selectedRelPaths() const;

    GitOperationManager *m_opMgr;
    QPointer<GitController> m_controller;
    GitOperationManager::OperationState m_opState = GitOperationManager::OperationState::Idle;

    QLabel *m_descLabel;
    QTreeView *m_tree;
    QStandardItemModel *m_model;
    QPushButton *m_continueBtn;
    QPushButton *m_abortBtn;
    QPushButton *m_skipBtn;

    ConflictEntries m_entries;
};

#endif // CONFLICT_LIST_DOCK_H

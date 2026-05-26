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

#include "ConflictListDock.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

ConflictListDock::ConflictListDock(GitOperationManager *opMgr, QWidget *parent)
    : QDockWidget(parent)
    , m_opMgr(opMgr)
{
    setObjectName(QStringLiteral("ConflictListDock"));
    setWindowTitle(tr("Conflicts"));

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_descLabel = new QLabel(this);
    m_descLabel->setWordWrap(true);
    layout->addWidget(m_descLabel);

    m_tree = new QTreeView(this);
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setAlternatingRowColors(true);
    layout->addWidget(m_tree, 1);

    m_model = new QStandardItemModel(0, 3, this);
    m_model->setHorizontalHeaderLabels({tr("File"), tr("Type"), tr("Status")});
    m_tree->setModel(m_model);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    auto *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(6);
    m_continueBtn = new QPushButton(tr("Continue"), this);
    m_abortBtn = new QPushButton(tr("Abort"), this);
    m_skipBtn = new QPushButton(tr("Skip"), this);
    btnLayout->addWidget(m_continueBtn);
    btnLayout->addWidget(m_skipBtn);
    btnLayout->addWidget(m_abortBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    setWidget(container);

    connect(m_tree, &QTreeView::doubleClicked, this, &ConflictListDock::onDoubleClicked);
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &ConflictListDock::onContextMenu);
    connect(m_continueBtn, &QPushButton::clicked, this, &ConflictListDock::onContinueClicked);
    connect(m_abortBtn, &QPushButton::clicked, this, &ConflictListDock::onAbortClicked);
    connect(m_skipBtn, &QPushButton::clicked, this, &ConflictListDock::onSkipClicked);

    updateButtonStates();
}

void ConflictListDock::setController(GitController *controller)
{
    m_controller = controller;
}

void ConflictListDock::setConflicts(const ConflictEntries &entries)
{
    m_entries = entries;
    m_model->removeRows(0, m_model->rowCount());

    for (const auto &e : entries) {
        auto *pathItem = new QStandardItem(e.relPath);
        pathItem->setEditable(false);

        QString typeStr;
        switch (e.type) {
        case ConflictEntry::BothModified:    typeStr = tr("Both Modified"); break;
        case ConflictEntry::ModifiedDeleted: typeStr = tr("Modified / Deleted"); break;
        case ConflictEntry::DeletedModified: typeStr = tr("Deleted / Modified"); break;
        case ConflictEntry::BothAdded:       typeStr = tr("Both Added"); break;
        }
        auto *typeItem = new QStandardItem(typeStr);
        typeItem->setEditable(false);

        auto *statusItem = new QStandardItem(tr("Unresolved"));
        statusItem->setEditable(false);

        m_model->appendRow({pathItem, typeItem, statusItem});
    }

    updateTitle();
    updateButtonStates();
}

void ConflictListDock::setOperationState(GitOperationManager::OperationState state)
{
    m_opState = state;
    updateButtonStates();
}

void ConflictListDock::setDescription(const QString &text)
{
    m_descLabel->setText(text);
    m_descLabel->setVisible(!text.isEmpty());
}

void ConflictListDock::onDoubleClicked(const QModelIndex &index)
{
    int row = index.row();
    if (row < 0 || row >= m_entries.size()) return;

    const auto &entry = m_entries[row];
    if (entry.type == ConflictEntry::BothModified || entry.type == ConflictEntry::BothAdded)
        emit openInMergeViewer(entry);
}

void ConflictListDock::onContextMenu(const QPoint &pos)
{
    QModelIndex idx = m_tree->indexAt(pos);
    if (!idx.isValid()) return;

    QStringList paths = selectedRelPaths();
    if (paths.isEmpty()) return;

    QMenu menu(this);
    if (m_entries.size() > idx.row() &&
        (m_entries[idx.row()].type == ConflictEntry::BothModified ||
         m_entries[idx.row()].type == ConflictEntry::BothAdded)) {
        menu.addAction(tr("Open in Merge Viewer"), this, [this, idx]() {
            if (idx.row() < m_entries.size())
                emit openInMergeViewer(m_entries[idx.row()]);
        });
        menu.addSeparator();
    }
    menu.addAction(tr("Accept Ours"), this, [this, paths]() {
        emit acceptOursRequested(paths);
    });
    menu.addAction(tr("Accept Theirs"), this, [this, paths]() {
        emit acceptTheirsRequested(paths);
    });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void ConflictListDock::onContinueClicked()
{
    emit continueRequested();
}

void ConflictListDock::onAbortClicked()
{
    emit abortRequested();
}

void ConflictListDock::onSkipClicked()
{
    emit skipRequested();
}

void ConflictListDock::updateTitle()
{
    if (m_entries.isEmpty())
        setWindowTitle(tr("Conflicts"));
    else
        setWindowTitle(tr("Conflicts (%1)").arg(m_entries.size()));
}

void ConflictListDock::updateButtonStates()
{
    bool isRebase = (m_opState == GitOperationManager::OperationState::RebaseSuspended ||
                     m_opState == GitOperationManager::OperationState::RebaseSuspendedEdit);
    bool isMerge = (m_opState == GitOperationManager::OperationState::MergeConflicted);
    bool hasConflicts = !m_entries.isEmpty();

    m_continueBtn->setVisible(isRebase || isMerge);
    m_continueBtn->setEnabled(!hasConflicts);
    m_continueBtn->setText(isMerge ? tr("Commit Merge") : tr("Continue Rebase"));

    m_abortBtn->setVisible(isRebase || isMerge);
    m_abortBtn->setEnabled(true);
    m_abortBtn->setText(isMerge ? tr("Abort Merge") : tr("Abort Rebase"));

    m_skipBtn->setVisible(isRebase);
    m_skipBtn->setEnabled(isRebase);
}

QStringList ConflictListDock::selectedRelPaths() const
{
    QStringList paths;
    const auto indexes = m_tree->selectionModel()->selectedRows(0);
    for (const auto &idx : indexes) {
        int row = idx.row();
        if (row >= 0 && row < m_entries.size())
            paths.append(m_entries[row].relPath);
    }
    if (paths.isEmpty() && !m_entries.isEmpty()) {
        for (const auto &e : m_entries)
            paths.append(e.relPath);
    }
    return paths;
}

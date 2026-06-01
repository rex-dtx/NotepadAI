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

#ifndef SSHREMOTEFOLDERPICKERDIALOG_H
#define SSHREMOTEFOLDERPICKERDIALOG_H

#include <QDialog>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>

#include "remote/SshSessionWorker.h" // remote::RemoteDirEntry (complete type for QList<>)

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace remote { class RemoteFsBackend; }

// Modal dialog that lets the user browse a remote SFTP filesystem and pick a
// directory as the workspace root. Populated lazily via
// RemoteFsBackend::readdirAsync. Code-built (no .ui), palette-driven, full
// keyboard nav (Tab order, Enter=select, Esc=cancel), visible focus, AA
// contrast. States: loading (status text), error (message), empty (expanded,
// no children), success (tree populated).
class SshRemoteFolderPickerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SshRemoteFolderPickerDialog(remote::RemoteFsBackend *backend,
                                         const QString &initialPath,
                                         QWidget *parent = nullptr);

    QString selectedPath() const { return m_selectedPath; }

private:
    void populateRoot();
    void fetchChildren(QTreeWidgetItem *item);
    void onReaddirResult(const QString &path, bool ok,
                         const QList<remote::RemoteDirEntry> &entries,
                         const QString &error);
    void onItemExpanded(QTreeWidgetItem *item);
    void onSelectionChanged();
    void onAccept();
    QString pathForItem(QTreeWidgetItem *item) const;

    QPointer<remote::RemoteFsBackend> m_backend;
    QString m_initialPath;
    QString m_selectedPath;

    QTreeWidget *m_tree = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_selectBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;

    // Absolute remote path → item, so an async readdir result resolves back to
    // the right node even if the tree changed in the meantime (O(1) lookup).
    QHash<QString, QTreeWidgetItem *> m_itemByPath;
    // Paths with a fetch currently in flight (guards double-fetch on expand).
    QHash<QString, bool> m_loading;
};

#endif // SSHREMOTEFOLDERPICKERDIALOG_H

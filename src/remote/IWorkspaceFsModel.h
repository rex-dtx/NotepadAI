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

#ifndef REMOTE_IWORKSPACEFSMODEL_H
#define REMOTE_IWORKSPACEFSMODEL_H

#include <QModelIndex>
#include <QString>

class QAbstractItemModel;

namespace remote {

// The minimal, path-based contract that FolderAsWorkspaceDock and
// FolderAsWorkspaceProxyModel actually consume from the workspace file-tree
// model (D2). It exists so the dock/proxy can drive EITHER the local
// QFileSystemModel-backed model OR the SFTP-backed RemoteFileSystemModel without
// knowing which — the synthetic-root proxy math (single m_root + m_rootPtr, the
// two O(1) guards, isInSubtree forwarding, columnCount()==1) never depended on
// QFileSystemModel beyond index(path)/filePath()/isDir(), which move here.
//
// Design notes:
//   - This is a PLAIN abstract base (NOT a QObject) so a concrete model can be a
//     QAbstractItemModel subclass that ALSO inherits this interface without a
//     diamond/double-QObject. Consumers recover the model via asModel() (for
//     setSourceModel) and the interface via dynamic_cast (for the path helpers).
//   - `directoryLoaded(const QString &path)` is NOT declared here (a non-QObject
//     cannot own signals). EVERY implementer is contractually required to be a
//     QAbstractItemModel subclass that emits a `directoryLoaded(QString)` signal
//     with that exact signature when a directory's children have materialized —
//     QFileSystemModel already does; RemoteFileSystemModel declares it. The
//     dock/proxy connect to it by signature off asModel() (string-based connect),
//     which resolves at runtime against whichever concrete model is in use.
//   - `indexForPath` is named distinctly from QAbstractItemModel::index(row,col,
//     parent) to avoid an overload clash on the concrete QFileSystemModel
//     subclass (whose path-based lookup is itself spelled `index(const QString&)`).
class IWorkspaceFsModel
{
public:
    virtual ~IWorkspaceFsModel() = default;

    // Resolve an absolute path to its model index. Valid synchronously only once
    // the path's PARENT directory has been listed (its row exists); returns an
    // invalid index otherwise, exactly like QFileSystemModel::index(path) — the
    // dock's path-space pending-expansion logic waits on directoryLoaded and
    // retries. The returned index is a SOURCE index (column 0).
    virtual QModelIndex indexForPath(const QString &path) const = 0;

    // Absolute filesystem (local) or remote-absolute (SFTP) path for an index.
    virtual QString filePath(const QModelIndex &index) const = 0;

    // True iff the index denotes a directory.
    virtual bool isDir(const QModelIndex &index) const = 0;

    // Re-root the model at `path`. The model fills children asynchronously and
    // emits directoryLoaded(path) as each directory's contents arrive (the local
    // model via its gatherer thread; the remote model via the SFTP worker).
    virtual void setRootPath(const QString &path) = 0;

    // The current root path (cleaned/absolute), or empty if none.
    virtual QString rootPath() const = 0;

    // The concrete QAbstractItemModel behind this interface (this same object).
    // Handed to QAbstractProxyModel::setSourceModel and used to connect the
    // directoryLoaded signal by signature.
    virtual QAbstractItemModel *asModel() = 0;
};

} // namespace remote

#endif // REMOTE_IWORKSPACEFSMODEL_H

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

#ifndef SSHDEBUGDIALOG_H
#define SSHDEBUGDIALOG_H

#include <QDialog>
#include <QPointer>

class QPlainTextEdit;

namespace remote { class SshConnection; }

// Non-modal SSH debug log dialog. Owned by MainWindow across workspace switches.
// Call bindToConnection() whenever the active SSH workspace changes. Uses
// QPointer<SshConnection> so the pointer is automatically nulled when the
// registry deletes the connection object, and the live-tail lambda guards on
// !m_connection to drop any already-queued debugLogAppended signals safely.
class SshDebugDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SshDebugDialog(QWidget *parent = nullptr);

    // Rebind to a (possibly null) connection. Disconnects the previous live-tail,
    // repopulates the log from the new buffer, and rewires the live-tail signal.
    // Pass nullptr to show a placeholder when no SSH workspace is active.
    void bindToConnection(remote::SshConnection *conn);

private slots:
    void onRefresh();
    void onCopy() const;
    void onClear();

private:
    // QPointer: auto-nulled by Qt when the SshConnection is deleted (inside
    // ~QObject, before queued signals can be dispatched). The live-tail lambda
    // checks !m_connection to silently drop stale deliveries.
    QPointer<remote::SshConnection> m_connection;
    QPlainTextEdit *m_text = nullptr;
};

#endif // SSHDEBUGDIALOG_H

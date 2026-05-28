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

#ifndef SCHEDULED_TASK_DIALOG_H
#define SCHEDULED_TASK_DIALOG_H

#include <QDialog>

class AcpAgentRegistry;
class ApplicationSettings;
class QTableWidget;
class ScheduledTaskRegistry;
class ScheduledTaskRunner;

class ScheduledTaskDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScheduledTaskDialog(ScheduledTaskRegistry *registry,
                                 ScheduledTaskRunner *runner,
                                 AcpAgentRegistry *agentRegistry,
                                 ApplicationSettings *settings,
                                 QWidget *parent = nullptr);

    void setDefaultWorkspace(const QString &cwd);
    void setRecentWorkspaces(const QStringList &paths);

private slots:
    void onAdd();
    void onEdit();
    void onRemove();
    void onRunNow();
    void refreshTable();

private:
    ScheduledTaskRegistry *m_registry;
    ScheduledTaskRunner *m_runner;
    AcpAgentRegistry *m_agentRegistry;
    ApplicationSettings *m_settings;
    QTableWidget *m_table;
    QString m_defaultWorkspace;
    QStringList m_recentWorkspaces;
};

#endif // SCHEDULED_TASK_DIALOG_H

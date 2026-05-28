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

#ifndef SCHEDULED_TASK_REGISTRY_H
#define SCHEDULED_TASK_REGISTRY_H

#include <QList>
#include <QObject>
#include <QString>

#include "ScheduledTaskDefinition.h"

class ApplicationSettings;

class ScheduledTaskRegistry : public QObject
{
    Q_OBJECT

public:
    explicit ScheduledTaskRegistry(ApplicationSettings *settings, QObject *parent = nullptr);

    QList<ScheduledTaskDefinition> tasks() const;
    ScheduledTaskDefinition task(const QString &id) const;

    bool addTask(const ScheduledTaskDefinition &def);
    bool updateTask(const ScheduledTaskDefinition &def);
    bool removeTask(const QString &id);
    void setEnabled(const QString &id, bool enabled);

signals:
    void changed();

private:
    void load();
    void persist();

    ApplicationSettings *m_settings;
    QList<ScheduledTaskDefinition> m_tasks;
};

#endif // SCHEDULED_TASK_REGISTRY_H

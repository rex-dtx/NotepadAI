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

#include "ScheduledTaskRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ApplicationSettings.h"

ScheduledTaskRegistry::ScheduledTaskRegistry(ApplicationSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    load();
}

void ScheduledTaskRegistry::load()
{
    m_tasks.clear();

    if (!m_settings) {
        return;
    }

    const QString json = m_settings->scheduledTasksJson();
    if (json.isEmpty()) {
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonArray arr = root.value(QStringLiteral("tasks")).toArray();
    for (const auto &v : arr) {
        if (!v.isObject()) {
            continue;
        }
        ScheduledTaskDefinition def = scheduledTaskDefinitionFromJson(v.toObject());
        if (def.id.isEmpty()) {
            continue;
        }
        m_tasks.append(def);
    }
}

void ScheduledTaskRegistry::persist()
{
    if (!m_settings) {
        return;
    }

    QJsonArray arr;
    for (const ScheduledTaskDefinition &def : m_tasks) {
        arr.append(scheduledTaskDefinitionToJson(def));
    }

    QJsonObject root;
    root.insert(QStringLiteral("tasks"), arr);

    const QJsonDocument doc(root);
    const QString compact = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    m_settings->setScheduledTasksJson(compact);
}

QList<ScheduledTaskDefinition> ScheduledTaskRegistry::tasks() const
{
    return m_tasks;
}

ScheduledTaskDefinition ScheduledTaskRegistry::task(const QString &id) const
{
    for (const ScheduledTaskDefinition &def : m_tasks) {
        if (def.id == id) {
            return def;
        }
    }
    return ScheduledTaskDefinition{};
}

bool ScheduledTaskRegistry::addTask(const ScheduledTaskDefinition &def)
{
    if (def.id.isEmpty()) {
        return false;
    }
    for (const ScheduledTaskDefinition &existing : m_tasks) {
        if (existing.id == def.id) {
            return false;
        }
    }
    m_tasks.append(def);
    persist();
    emit changed();
    return true;
}

bool ScheduledTaskRegistry::updateTask(const ScheduledTaskDefinition &def)
{
    if (def.id.isEmpty()) {
        return false;
    }
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].id == def.id) {
            m_tasks[i] = def;
            persist();
            emit changed();
            return true;
        }
    }
    return false;
}

bool ScheduledTaskRegistry::removeTask(const QString &id)
{
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].id == id) {
            m_tasks.removeAt(i);
            persist();
            emit changed();
            return true;
        }
    }
    return false;
}

void ScheduledTaskRegistry::setEnabled(const QString &id, bool enabled)
{
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].id == id) {
            if (m_tasks[i].enabled != enabled) {
                m_tasks[i].enabled = enabled;
                persist();
                emit changed();
            }
            return;
        }
    }
}

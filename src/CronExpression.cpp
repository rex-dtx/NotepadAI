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

#include "CronExpression.h"

#include <cstring>
#include <ctime>

std::optional<CronExpression> CronExpression::parse(const QString &expr)
{
    const QByteArray utf8 = expr.trimmed().toUtf8();
    const char *err = nullptr;

    CronExpression result;
    std::memset(&result.m_expr, 0, sizeof(result.m_expr));
    cron_parse_expr(utf8.constData(), &result.m_expr, &err);

    if (err != nullptr) {
        return std::nullopt;
    }

    result.m_valid = true;
    return result;
}

QDateTime CronExpression::nextFireTime(const QDateTime &from) const
{
    if (!m_valid || !from.isValid()) {
        return QDateTime();
    }

    const std::time_t fromEpoch = static_cast<std::time_t>(from.toSecsSinceEpoch());
    const std::time_t next = cron_next(const_cast<cron_expr *>(&m_expr), fromEpoch);

    if (next == static_cast<std::time_t>(-1)) {
        return QDateTime();
    }

    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(next), Qt::LocalTime);
}

QList<QDateTime> CronExpression::nextFireTimes(const QDateTime &from, int count) const
{
    QList<QDateTime> results;
    if (!m_valid || count <= 0 || !from.isValid()) {
        return results;
    }

    results.reserve(count);
    QDateTime current = from;

    for (int i = 0; i < count; ++i) {
        const QDateTime next = nextFireTime(current);
        if (!next.isValid()) {
            break;
        }
        results.append(next);
        current = next;
    }

    return results;
}

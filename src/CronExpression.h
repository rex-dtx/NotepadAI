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

#ifndef CRON_EXPRESSION_H
#define CRON_EXPRESSION_H

#include <optional>

#include <QDateTime>
#include <QList>
#include <QString>

#include "ccronexpr.h"

// Thin Qt wrapper around supertinycron's cron_expr. Immutable after parse.
// Thread-safe for reads (no mutable state after construction).
class CronExpression
{
public:
    // Parse a standard 5-field cron expression. Returns std::nullopt on failure.
    static std::optional<CronExpression> parse(const QString &expr);

    // Returns the next fire time strictly after `from`. Returns an invalid
    // QDateTime if no valid next time exists (e.g. impossible date).
    QDateTime nextFireTime(const QDateTime &from) const;

    // Returns up to `count` successive fire times starting strictly after `from`.
    QList<QDateTime> nextFireTimes(const QDateTime &from, int count) const;

    bool isValid() const { return m_valid; }

private:
    CronExpression() = default;

    cron_expr m_expr{};
    bool m_valid = false;
};

#endif // CRON_EXPRESSION_H

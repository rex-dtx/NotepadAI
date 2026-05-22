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

#ifndef GIT_ERROR_CLASSIFIER_H
#define GIT_ERROR_CLASSIFIER_H

#include "GitError.h"

#include <QByteArray>
#include <QStringList>

class GitErrorClassifier
{
public:
    // Map a non-zero git exit + raw stderr into a typed GitError.
    // argv is the argv that was run (for context-sensitive hints).
    static GitError classify(int exitCode, const QByteArray &stderr_, const QStringList &argv);
};

#endif // GIT_ERROR_CLASSIFIER_H

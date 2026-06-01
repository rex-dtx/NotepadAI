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

#include "TerminalCwdResolver.h"

#include "remote/ExecutionContext.h"
#include "remote/SshProfile.h"

#include <QDir>
#include <QFileInfo>

static bool directoryExists(const QString &path)
{
    if (path.isEmpty() || remote::isSshUri(path)) {
        return false;
    }
    QFileInfo info(path);
    return info.exists() && info.isDir();
}

bool TerminalCwdResolver::canOpenInWorkspace(const QString &workspaceRoot)
{
    return directoryExists(workspaceRoot);
}

bool TerminalCwdResolver::canOpenInFolder(const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot)
{
    if (activeBufferIsFile && !activeFilePath.isEmpty() && !remote::isSshUri(activeFilePath)) {
        const QString parent = QFileInfo(activeFilePath).absolutePath();
        if (directoryExists(parent)) {
            return true;
        }
    }
    return directoryExists(workspaceRoot);
}

QString TerminalCwdResolver::resolveWorkspace(const QString &workspaceRoot)
{
    if (directoryExists(workspaceRoot)) {
        return QDir::cleanPath(workspaceRoot);
    }
    return QString();
}

QString TerminalCwdResolver::resolveFolder(const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot)
{
    if (activeBufferIsFile && !activeFilePath.isEmpty() && !remote::isSshUri(activeFilePath)) {
        const QString parent = QFileInfo(activeFilePath).absolutePath();
        if (directoryExists(parent)) {
            return QDir::cleanPath(parent);
        }
    }
    if (directoryExists(workspaceRoot)) {
        return QDir::cleanPath(workspaceRoot);
    }
    return QString();
}

static QString posixParentPath(QString path)
{
    if (path.isEmpty()) {
        return QString();
    }
    if (!path.startsWith(QLatin1Char('/'))) {
        path.prepend(QLatin1Char('/'));
    }
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash > 0 ? path.left(slash) : QStringLiteral("/");
}

static QString remoteWorkspacePath(const QString &workspaceRoot)
{
    const remote::SshUri uri = remote::parseSshUri(workspaceRoot);
    return uri.valid ? uri.remotePath : QString();
}

static QString remoteFolderPath(const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot)
{
    const remote::SshUri workspaceUri = remote::parseSshUri(workspaceRoot);
    if (!workspaceUri.valid) {
        return QString();
    }
    if (activeBufferIsFile && remote::isSshUri(activeFilePath)) {
        const remote::SshUri fileUri = remote::parseSshUri(activeFilePath);
        if (fileUri.valid && fileUri.profileId == workspaceUri.profileId) {
            return posixParentPath(fileUri.remotePath);
        }
    }
    return workspaceUri.remotePath;
}

bool TerminalCwdResolver::canOpenInWorkspaceForContext(remote::ExecutionContext *ctx, const QString &workspaceRoot)
{
    if (!ctx || !ctx->isRemote()) {
        return canOpenInWorkspace(workspaceRoot);
    }
    return !resolveWorkspaceForContext(ctx, workspaceRoot).isEmpty();
}

bool TerminalCwdResolver::canOpenInFolderForContext(remote::ExecutionContext *ctx, const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot)
{
    if (!ctx || !ctx->isRemote()) {
        return canOpenInFolder(activeFilePath, activeBufferIsFile, workspaceRoot);
    }
    return !resolveFolderForContext(ctx, activeFilePath, activeBufferIsFile, workspaceRoot).isEmpty();
}

QString TerminalCwdResolver::resolveWorkspaceForContext(remote::ExecutionContext *ctx, const QString &workspaceRoot)
{
    if (!ctx || !ctx->isRemote()) {
        return resolveWorkspace(workspaceRoot);
    }
    return resolveForContext(ctx, remoteWorkspacePath(workspaceRoot));
}

QString TerminalCwdResolver::resolveFolderForContext(remote::ExecutionContext *ctx, const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot)
{
    if (!ctx || !ctx->isRemote()) {
        return resolveFolder(activeFilePath, activeBufferIsFile, workspaceRoot);
    }
    return resolveForContext(ctx, remoteFolderPath(activeFilePath, activeBufferIsFile, workspaceRoot));
}

QString TerminalCwdResolver::resolveForContext(remote::ExecutionContext *ctx, const QString &requested)
{
    // Local / no context → the existing local behavior verbatim. (A null ctx is
    // treated as local; this keeps callers that have no context wired yet on
    // the identical path.)
    if (!ctx || !ctx->isRemote()) {
        return resolveWorkspace(requested);
    }

    // Remote: never stat the path against the LOCAL disk (it lives on another
    // machine). Require the connection to be live and the path non-empty, then
    // delegate to the context's own POSIX normalization (design D11). The caller
    // supplies the remote default (lastRemotePath or "~") as `requested`.
    if (ctx->state() != remote::ExecutionContext::State::Connected) {
        return QString();
    }
    if (requested.isEmpty()) {
        return QString();
    }
    return ctx->resolveCwd(requested);
}

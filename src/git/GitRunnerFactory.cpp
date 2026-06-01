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

#include "GitRunnerFactory.h"

#include "GitProcessRunner.h"

#include "../NotepadNextApplication.h"
#include "remote/ExecutionContext.h"
#include "remote/ExecutionContextRegistry.h"
#include "remote/RemoteExecutionContext.h"
#include "remote/SshProfile.h" // isSshUri / parseSshUri

#include <QCoreApplication>

namespace {

// Resolve the live REMOTE context for an ssh:// repoPath, or nullptr for any
// local path / missing app / not-yet-connected profile. Connected-only: a
// profile that exists but is still Connecting/Failed is treated as not remote
// so we never hand back a runner that would immediately fail — the caller
// falls back to local, matching the "default to local when ambiguous" rule.
remote::RemoteExecutionContext *resolveRemote(const QString &repoPath)
{
    if (!remote::isSshUri(repoPath)) {
        return nullptr;
    }
    const remote::SshUri uri = remote::parseSshUri(repoPath);
    if (!uri.valid) {
        return nullptr;
    }
    auto *app = qobject_cast<NotepadNextApplication *>(QCoreApplication::instance());
    if (!app) {
        return nullptr;
    }
    remote::ExecutionContextRegistry *registry = app->getExecutionContextRegistry();
    if (!registry) {
        return nullptr;
    }
    remote::RemoteExecutionContext *ctx = registry->remoteContext(uri.profileId);
    if (!ctx || ctx->state() != remote::ExecutionContext::State::Connected) {
        return nullptr;
    }
    return ctx;
}

} // namespace

namespace GitRunnerFactory {

IGitProcessRunner *createForRepo(const QString &repoPath, QObject *parent)
{
    if (remote::RemoteExecutionContext *ctx = resolveRemote(repoPath)) {
        if (IGitProcessRunner *runner = ctx->createGitRunner(parent)) {
            return runner;
        }
    }
    // Default / fallback: the exact local runner used before this change.
    return new GitProcessRunner(parent);
}

bool isRemoteRepo(const QString &repoPath)
{
    return resolveRemote(repoPath) != nullptr;
}

} // namespace GitRunnerFactory

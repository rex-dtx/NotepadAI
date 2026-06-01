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
#include "remote/RemoteGitProcessRunner.h"
#include "remote/SshProfile.h" // isSshUri / parseSshUri

#include <QCoreApplication>

namespace {

QString extractProfileId(const QString &repoPath)
{
    if (!remote::isSshUri(repoPath)) return {};
    const remote::SshUri uri = remote::parseSshUri(repoPath);
    return uri.valid ? uri.profileId : QString();
}

remote::RemoteExecutionContext *resolveConnectedContext(const QString &repoPath)
{
    const QString profileId = extractProfileId(repoPath);
    if (profileId.isEmpty()) return nullptr;

    auto *app = qobject_cast<NotepadNextApplication *>(QCoreApplication::instance());
    if (!app) return nullptr;
    remote::ExecutionContextRegistry *registry = app->getExecutionContextRegistry();
    if (!registry) return nullptr;
    remote::RemoteExecutionContext *ctx = registry->remoteContext(profileId);
    if (!ctx || ctx->state() != remote::ExecutionContext::State::Connected) return nullptr;
    return ctx;
}

} // namespace

namespace GitRunnerFactory {

IGitProcessRunner *createForRepo(const QString &repoPath, QObject *parent)
{
    const QString profileId = extractProfileId(repoPath);
    if (profileId.isEmpty()) {
        return new GitProcessRunner(parent);
    }

    // Known-remote (ssh:// URI). Try to get a live runner.
    auto *app = qobject_cast<NotepadNextApplication *>(QCoreApplication::instance());
    remote::ExecutionContextRegistry *registry =
        app ? app->getExecutionContextRegistry() : nullptr;
    remote::RemoteExecutionContext *ctx =
        registry ? registry->remoteContext(profileId) : nullptr;

    if (ctx && ctx->state() == remote::ExecutionContext::State::Connected) {
        if (IGitProcessRunner *runner = ctx->createGitRunner(parent)) {
            return runner;
        }
    }

    // Known-remote but not connected: return a null-connection remote runner
    // that fails loudly ("no SSH connection") on every run() call. Never fall
    // back to a local runner — running local git on a remote POSIX path is
    // meaningless and would produce confusing errors.
    return new remote::RemoteGitProcessRunner(nullptr, parent);
}

bool isRemoteRepo(const QString &repoPath)
{
    return resolveConnectedContext(repoPath) != nullptr;
}

bool isRemotePath(const QString &path)
{
    return remote::isSshUri(path);
}

} // namespace GitRunnerFactory

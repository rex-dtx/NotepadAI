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

#ifndef GIT_RUNNER_FACTORY_H
#define GIT_RUNNER_FACTORY_H

#include <QString>

class QObject;
class IGitProcessRunner;

// The single seam that resolves a git runner for a repository path through the
// owning workspace's ExecutionContext (D6). Every git invocation site obtains
// its runner here instead of `new GitProcessRunner` directly, so a local
// workspace transparently gets the local QProcess-backed runner and a remote
// (ssh://) workspace gets a RemoteGitProcessRunner over the multiplexed SSH
// connection.
//
// Resolution policy — DEFAULT TO LOCAL when anything is ambiguous:
//   * repoPath is an `ssh://<profileId><path>` URI AND that profile has a live,
//     connected RemoteExecutionContext  → remote runner.
//   * everything else (local path, no app/registry, profile not connected) →
//     local GitProcessRunner. Behavior is byte-for-byte the prior local path.
//
// The factory reaches the app-wide ExecutionContextRegistry via the
// NotepadNextApplication singleton; with no app instance (unit tests / headless)
// it always returns a local runner, so callers never depend on the registry.
namespace GitRunnerFactory {

// Create a runner for a git op targeting `repoPath`. Never returns nullptr —
// falls back to a local GitProcessRunner. `parent` owns the returned runner.
IGitProcessRunner *createForRepo(const QString &repoPath, QObject *parent);

// True iff `repoPath` resolves to a live REMOTE execution context. Used by the
// UI to gate remote-incapable actions (e.g. interactive rebase, D7). False for
// any local path or when no remote context is connected.
bool isRemoteRepo(const QString &repoPath);

} // namespace GitRunnerFactory

#endif // GIT_RUNNER_FACTORY_H

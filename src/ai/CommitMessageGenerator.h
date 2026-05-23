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

#ifndef AI_COMMIT_MESSAGE_GENERATOR_H
#define AI_COMMIT_MESSAGE_GENERATOR_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <cstdint>

class CommitComposer;
class ApplicationSettings;

namespace ai {

class ILlmHttpClient;
class CredentialStore;

// Singleton owned by NotepadNextApplication. Coordinates the AI-driven commit
// message generation flow for the currently-active CommitComposer:
//
//   Idle  ─trigger→  Authenticating  ─first byte→  Streaming  ─done→  Idle
//                       │                              │
//                       └────────── error/cancel ──────┴────→  (Idle or Error)
//
// At most one generation is in flight across the entire app. Switching the
// active repo or workspace cancels the in-flight stream; the text already
// appended to the previous composer is preserved (lives in the composer's
// own buffer + the per-repo draft persistence handled by GitTabWidget).
//
// Token chunks are coalesced through a single reusable QTimer (interval 0,
// single-shot) so we get exactly one insertText per event-loop tick regardless
// of network chunk rate — zero per-chunk QObject allocation.
class CommitMessageGenerator : public QObject
{
    Q_OBJECT
public:
    enum class State : std::uint8_t { Idle, Authenticating, Streaming, Cancelling, Error };
    Q_ENUM(State)

    explicit CommitMessageGenerator(ApplicationSettings *settings,
                                    CredentialStore *credStore,
                                    QObject *parent = nullptr);
    ~CommitMessageGenerator() override;

    // Inject a mock HTTP client (takes ownership). Existing prod client deleted.
    void setHttpClientForTesting(ILlmHttpClient *client);

    State state() const { return m_state; }
    QString currentRepoKey() const { return m_currentRepoKey; }

    // True when the generator is ready to accept a trigger() for this repo +
    // composer combination (no in-flight stream; URL/model/key configured;
    // diff non-empty handled by caller via canFireGenerate).
    bool canFireGenerate(const QString &workspaceRoot,
                         CommitComposer *composer,
                         QString *whyNot = nullptr) const;

public slots:
    // Start a generation. `subjectHint` may be empty. `compressedDiff` is the
    // already-fetched + compressed diff bytes; the caller computes those
    // because they live in GitController-land.
    void trigger(const QString &workspaceRoot,
                 const QString &submoduleRoot,
                 CommitComposer *composer,
                 const QString &subjectHint,
                 const QByteArray &compressedDiff);

    // Cancel any in-flight stream. No-op when Idle.
    void cancel();

    // Cancel iff the target composer matches `composer`. Wired to repo-combo
    // switch and workspace-dock switch, so generations driven from a different
    // composer are not disturbed.
    void cancelIfTarget(CommitComposer *composer);

signals:
    void stateChanged(ai::CommitMessageGenerator::State state);
    void errorOccurred(const QString &message);

private slots:
    void onFirstByte();
    void onToken(const QString &chunk);
    void onStreamEnded();
    void onStreamError(int httpStatus, const QString &message);
    void flushPending();

private:
    void setState(State s);
    void appendChunk(const QString &chunk);
    void teardownAfterError();

    ApplicationSettings *m_settings = nullptr;
    CredentialStore     *m_credStore = nullptr;
    ILlmHttpClient      *m_http = nullptr;

    State                m_state = State::Idle;
    QPointer<CommitComposer> m_target;
    QString              m_currentRepoKey;        // identifier (workspaceRoot for now)
    bool                 m_hadSubjectHint = false;
    bool                 m_firstChunkSeen = false;

    QString              m_pendingTokens;         // coalesce buffer
    QTimer               m_flushTimer;            // reusable, zero-alloc per chunk
};

} // namespace ai

#endif // AI_COMMIT_MESSAGE_GENERATOR_H

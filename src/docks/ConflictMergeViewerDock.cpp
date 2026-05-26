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

#include "ConflictMergeViewerDock.h"
#include "BufferDiffEngine.h"
#include "EditorManager.h"
#include "ScintillaNext.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <Scintilla.h>

static constexpr int MARKER_CONFLICT = 25;
static constexpr int MARKER_RESOLVED = 26;
static constexpr int INDIC_CONFLICT_BG = 20;

ConflictMergeViewerDock::ConflictMergeViewerDock(const ConflictData &data,
                                                 EditorManager *editorMgr,
                                                 QWidget *parent)
    : QDockWidget(parent)
    , m_filePath(data.filePath)
    , m_editorMgr(editorMgr)
{
    setObjectName(QStringLiteral("ConflictMergeViewer_") + data.filePath);
    setAttribute(Qt::WA_DeleteOnClose);

    QString fileName = data.filePath.mid(data.filePath.lastIndexOf(QLatin1Char('/')) + 1);
    setWindowTitle(tr("Merge: %1").arg(fileName));

    auto *container = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(2);

    // Header labels
    auto *headerLayout = new QHBoxLayout;
    m_leftLabel = new QLabel(data.leftLabel.isEmpty() ? tr("Ours") : data.leftLabel);
    m_centerLabel = new QLabel(tr("Result"));
    m_rightLabel = new QLabel(data.rightLabel.isEmpty() ? tr("Theirs") : data.rightLabel);
    m_leftLabel->setAlignment(Qt::AlignCenter);
    m_centerLabel->setAlignment(Qt::AlignCenter);
    m_rightLabel->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(m_leftLabel, 1);
    headerLayout->addWidget(m_centerLabel, 1);
    headerLayout->addWidget(m_rightLabel, 1);
    mainLayout->addLayout(headerLayout);

    // Three-panel splitter
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_leftEditor = m_editorMgr->createEditor(QStringLiteral("merge-left"));
    m_centerEditor = m_editorMgr->createEditor(QStringLiteral("merge-center"));
    m_rightEditor = m_editorMgr->createEditor(QStringLiteral("merge-right"));

    setupEditor(m_leftEditor, true);
    setupEditor(m_centerEditor, false);
    setupEditor(m_rightEditor, true);

    m_splitter->addWidget(m_leftEditor);
    m_splitter->addWidget(m_centerEditor);
    m_splitter->addWidget(m_rightEditor);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 1);
    mainLayout->addWidget(m_splitter, 1);

    // Bottom toolbar
    auto *toolbarLayout = new QHBoxLayout;
    toolbarLayout->addStretch();
    m_resolveBtn = new QPushButton(tr("Mark Resolved"), this);
    toolbarLayout->addWidget(m_resolveBtn);
    mainLayout->addLayout(toolbarLayout);

    setWidget(container);

    // Load content
    m_leftEditor->setText(QString::fromUtf8(data.oursContent).toUtf8().constData());
    m_centerEditor->setText(QString::fromUtf8(data.oursContent).toUtf8().constData());
    m_rightEditor->setText(QString::fromUtf8(data.theirsContent).toUtf8().constData());

    m_leftEditor->emptyUndoBuffer();
    m_centerEditor->emptyUndoBuffer();
    m_rightEditor->emptyUndoBuffer();

    // Build diff and sync map
    auto leftHunks = BufferDiffEngine::diff(
        QByteArrayView(data.oursContent), QByteArrayView(data.oursContent));
    auto rightHunks = BufferDiffEngine::diff(
        QByteArrayView(data.theirsContent), QByteArrayView(data.oursContent));

    qint32 leftLines = static_cast<qint32>(m_leftEditor->lineCount());
    qint32 centerLines = static_cast<qint32>(m_centerEditor->lineCount());
    qint32 rightLines = static_cast<qint32>(m_rightEditor->lineCount());

    // For the scroll map: left-to-center is identity (center starts as ours),
    // right-to-center is the diff between theirs and ours.
    m_scrollMap.build(BufferDiffEngine::Hunks{}, rightHunks,
                      leftLines, centerLines, rightLines);

    buildChunks();
    highlightChunks();

    // Connect scroll sync
    connect(m_leftEditor, &ScintillaNext::updateUi, this, [this](Scintilla::Update flags) {
        if (static_cast<int>(flags) & SC_UPDATE_V_SCROLL) onLeftScrolled();
    });
    connect(m_centerEditor, &ScintillaNext::updateUi, this, [this](Scintilla::Update flags) {
        if (static_cast<int>(flags) & SC_UPDATE_V_SCROLL) onCenterScrolled();
    });
    connect(m_rightEditor, &ScintillaNext::updateUi, this, [this](Scintilla::Update flags) {
        if (static_cast<int>(flags) & SC_UPDATE_V_SCROLL) onRightScrolled();
    });

    connect(m_resolveBtn, &QPushButton::clicked, this, &ConflictMergeViewerDock::markResolved);
}

ConflictMergeViewerDock::~ConflictMergeViewerDock() = default;

QByteArray ConflictMergeViewerDock::centerContent() const
{
    return m_centerEditor->getText(m_centerEditor->length());
}

bool ConflictMergeViewerDock::hasUnresolvedChunks() const
{
    for (const auto &c : m_chunks)
        if (!c.resolved) return true;
    return false;
}

void ConflictMergeViewerDock::acceptChunkLeft(int chunkIndex)
{
    if (chunkIndex < 0 || chunkIndex >= m_chunks.size()) return;
    auto &chunk = m_chunks[chunkIndex];

    QByteArray leftBuf = m_leftEditor->getText(m_leftEditor->length());

    // Extract left chunk lines
    int startPos = static_cast<int>(m_leftEditor->positionFromLine(chunk.leftStart));
    int endPos = static_cast<int>(m_leftEditor->positionFromLine(chunk.leftEnd));
    QByteArray leftChunk = leftBuf.mid(startPos, endPos - startPos);

    // Replace in center
    int cStartPos = static_cast<int>(m_centerEditor->positionFromLine(chunk.centerStart));
    int cEndPos = static_cast<int>(m_centerEditor->positionFromLine(chunk.centerEnd));
    m_centerEditor->setTargetStart(cStartPos);
    m_centerEditor->setTargetEnd(cEndPos);
    m_centerEditor->replaceTarget(leftChunk.size(), leftChunk.constData());

    chunk.resolved = true;
    highlightChunks();
}

void ConflictMergeViewerDock::acceptChunkRight(int chunkIndex)
{
    if (chunkIndex < 0 || chunkIndex >= m_chunks.size()) return;
    auto &chunk = m_chunks[chunkIndex];

    QByteArray rightBuf = m_rightEditor->getText(m_rightEditor->length());

    int startPos = static_cast<int>(m_rightEditor->positionFromLine(chunk.rightStart));
    int endPos = static_cast<int>(m_rightEditor->positionFromLine(chunk.rightEnd));
    QByteArray rightChunk = rightBuf.mid(startPos, endPos - startPos);

    int cStartPos = static_cast<int>(m_centerEditor->positionFromLine(chunk.centerStart));
    int cEndPos = static_cast<int>(m_centerEditor->positionFromLine(chunk.centerEnd));
    m_centerEditor->setTargetStart(cStartPos);
    m_centerEditor->setTargetEnd(cEndPos);
    m_centerEditor->replaceTarget(rightChunk.size(), rightChunk.constData());

    chunk.resolved = true;
    highlightChunks();
}

void ConflictMergeViewerDock::acceptChunkBothLeftFirst(int chunkIndex)
{
    if (chunkIndex < 0 || chunkIndex >= m_chunks.size()) return;
    auto &chunk = m_chunks[chunkIndex];

    QByteArray leftBuf = m_leftEditor->getText(m_leftEditor->length());
    QByteArray rightBuf = m_rightEditor->getText(m_rightEditor->length());

    int lStart = static_cast<int>(m_leftEditor->positionFromLine(chunk.leftStart));
    int lEnd = static_cast<int>(m_leftEditor->positionFromLine(chunk.leftEnd));
    int rStart = static_cast<int>(m_rightEditor->positionFromLine(chunk.rightStart));
    int rEnd = static_cast<int>(m_rightEditor->positionFromLine(chunk.rightEnd));

    QByteArray combined = leftBuf.mid(lStart, lEnd - lStart) + rightBuf.mid(rStart, rEnd - rStart);

    int cStartPos = static_cast<int>(m_centerEditor->positionFromLine(chunk.centerStart));
    int cEndPos = static_cast<int>(m_centerEditor->positionFromLine(chunk.centerEnd));
    m_centerEditor->setTargetStart(cStartPos);
    m_centerEditor->setTargetEnd(cEndPos);
    m_centerEditor->replaceTarget(combined.size(), combined.constData());

    chunk.resolved = true;
    highlightChunks();
}

void ConflictMergeViewerDock::acceptChunkBothRightFirst(int chunkIndex)
{
    if (chunkIndex < 0 || chunkIndex >= m_chunks.size()) return;
    auto &chunk = m_chunks[chunkIndex];

    QByteArray leftBuf = m_leftEditor->getText(m_leftEditor->length());
    QByteArray rightBuf = m_rightEditor->getText(m_rightEditor->length());

    int lStart = static_cast<int>(m_leftEditor->positionFromLine(chunk.leftStart));
    int lEnd = static_cast<int>(m_leftEditor->positionFromLine(chunk.leftEnd));
    int rStart = static_cast<int>(m_rightEditor->positionFromLine(chunk.rightStart));
    int rEnd = static_cast<int>(m_rightEditor->positionFromLine(chunk.rightEnd));

    QByteArray combined = rightBuf.mid(rStart, rEnd - rStart) + leftBuf.mid(lStart, lEnd - lStart);

    int cStartPos = static_cast<int>(m_centerEditor->positionFromLine(chunk.centerStart));
    int cEndPos = static_cast<int>(m_centerEditor->positionFromLine(chunk.centerEnd));
    m_centerEditor->setTargetStart(cStartPos);
    m_centerEditor->setTargetEnd(cEndPos);
    m_centerEditor->replaceTarget(combined.size(), combined.constData());

    chunk.resolved = true;
    highlightChunks();
}

void ConflictMergeViewerDock::markResolved()
{
    emit resolved(m_filePath, centerContent());
    close();
}

void ConflictMergeViewerDock::closeEvent(QCloseEvent *event)
{
    if (hasUnresolvedChunks() && m_centerEditor->modify()) {
        auto result = QMessageBox::question(this, tr("Discard Changes?"),
            tr("This file has unresolved conflicts. Discard your edits?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (result == QMessageBox::No) {
            event->ignore();
            return;
        }
    }
    emit cancelled(m_filePath);
    QDockWidget::closeEvent(event);
}

void ConflictMergeViewerDock::onLeftScrolled()
{
    syncFrom(SyncPanel::Left);
}

void ConflictMergeViewerDock::onCenterScrolled()
{
    syncFrom(SyncPanel::Center);
}

void ConflictMergeViewerDock::onRightScrolled()
{
    syncFrom(SyncPanel::Right);
}

void ConflictMergeViewerDock::syncFrom(SyncPanel source)
{
    if (m_syncScrolling) return;
    m_syncScrolling = true;

    ScintillaNext *srcEditor = nullptr;
    switch (source) {
    case SyncPanel::Left:   srcEditor = m_leftEditor; break;
    case SyncPanel::Center: srcEditor = m_centerEditor; break;
    case SyncPanel::Right:  srcEditor = m_rightEditor; break;
    }

    qint32 srcLine = static_cast<qint32>(srcEditor->firstVisibleLine());

    if (source != SyncPanel::Left) {
        qint32 leftLine = m_scrollMap.translate(source, srcLine, SyncPanel::Left);
        m_leftEditor->setFirstVisibleLine(leftLine);
    }
    if (source != SyncPanel::Center) {
        qint32 centerLine = m_scrollMap.translate(source, srcLine, SyncPanel::Center);
        m_centerEditor->setFirstVisibleLine(centerLine);
    }
    if (source != SyncPanel::Right) {
        qint32 rightLine = m_scrollMap.translate(source, srcLine, SyncPanel::Right);
        m_rightEditor->setFirstVisibleLine(rightLine);
    }

    m_syncScrolling = false;
}

void ConflictMergeViewerDock::buildChunks()
{
    m_chunks.clear();

    QByteArray leftBuf = m_leftEditor->getText(m_leftEditor->length());
    QByteArray rightBuf = m_rightEditor->getText(m_rightEditor->length());

    auto hunks = BufferDiffEngine::diff(
        QByteArrayView(leftBuf.constData(), leftBuf.size()),
        QByteArrayView(rightBuf.constData(), rightBuf.size()));

    for (const auto &h : hunks) {
        Chunk c;
        c.leftStart = h.oldStart;
        c.leftEnd = h.oldStart + h.oldCount;
        c.rightStart = h.newStart;
        c.rightEnd = h.newStart + h.newCount;
        // Center starts as a copy of left, so center lines = left lines
        c.centerStart = h.oldStart;
        c.centerEnd = h.oldStart + h.oldCount;
        c.resolved = false;
        m_chunks.append(c);
    }
}

void ConflictMergeViewerDock::highlightChunks()
{
    // Clear existing markers
    m_leftEditor->markerDeleteAll(MARKER_CONFLICT);
    m_leftEditor->markerDeleteAll(MARKER_RESOLVED);
    m_rightEditor->markerDeleteAll(MARKER_CONFLICT);
    m_rightEditor->markerDeleteAll(MARKER_RESOLVED);
    m_centerEditor->markerDeleteAll(MARKER_CONFLICT);
    m_centerEditor->markerDeleteAll(MARKER_RESOLVED);

    for (const auto &c : m_chunks) {
        int marker = c.resolved ? MARKER_RESOLVED : MARKER_CONFLICT;
        for (qint32 line = c.leftStart; line < c.leftEnd; ++line)
            m_leftEditor->markerAdd(line, marker);
        for (qint32 line = c.rightStart; line < c.rightEnd; ++line)
            m_rightEditor->markerAdd(line, marker);
        for (qint32 line = c.centerStart; line < c.centerEnd; ++line)
            m_centerEditor->markerAdd(line, marker);
    }
}

void ConflictMergeViewerDock::setupEditor(ScintillaNext *editor, bool readOnly)
{
    editor->setReadOnly(readOnly);
    editor->setMarginWidthN(0, 40); // line numbers
    editor->setMarginWidthN(1, 0);  // hide default symbol margin

    // Conflict marker: red background
    editor->markerDefine(MARKER_CONFLICT, SC_MARK_BACKGROUND);
    editor->markerSetBack(MARKER_CONFLICT, 0xCCDDFF); // light red (BGR)

    // Resolved marker: green background
    editor->markerDefine(MARKER_RESOLVED, SC_MARK_BACKGROUND);
    editor->markerSetBack(MARKER_RESOLVED, 0xCCFFCC); // light green (BGR)
}

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

#ifndef CONFLICT_MERGE_VIEWER_DOCK_H
#define CONFLICT_MERGE_VIEWER_DOCK_H

#include "ConflictEntry.h"
#include "SyncScrollMap.h"

#include <QDockWidget>
#include <QVector>

class EditorManager;
class ScintillaNext;
class QSplitter;
class QLabel;
class QPushButton;

class ConflictMergeViewerDock : public QDockWidget
{
    Q_OBJECT
public:
    ConflictMergeViewerDock(const ConflictData &data, EditorManager *editorMgr,
                           QWidget *parent = nullptr);
    ~ConflictMergeViewerDock() override;

    QString filePath() const { return m_filePath; }
    QByteArray centerContent() const;
    bool hasUnresolvedChunks() const;

    static Qt::DockWidgetArea defaultArea() { return Qt::BottomDockWidgetArea; }

signals:
    void resolved(const QString &filePath, const QByteArray &content);
    void cancelled(const QString &filePath);

public slots:
    void acceptChunkLeft(int chunkIndex);
    void acceptChunkRight(int chunkIndex);
    void acceptChunkBothLeftFirst(int chunkIndex);
    void acceptChunkBothRightFirst(int chunkIndex);
    void markResolved();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onLeftScrolled();
    void onCenterScrolled();
    void onRightScrolled();

private:
    struct Chunk {
        qint32 leftStart, leftEnd;
        qint32 centerStart, centerEnd;
        qint32 rightStart, rightEnd;
        bool resolved = false;
    };

    void buildChunks();
    void highlightChunks();
    void syncFrom(SyncPanel source);
    void setupEditor(ScintillaNext *editor, bool readOnly);

    QString m_filePath;
    EditorManager *m_editorMgr;

    ScintillaNext *m_leftEditor = nullptr;
    ScintillaNext *m_centerEditor = nullptr;
    ScintillaNext *m_rightEditor = nullptr;

    QLabel *m_leftLabel;
    QLabel *m_centerLabel;
    QLabel *m_rightLabel;

    QSplitter *m_splitter;
    QPushButton *m_resolveBtn;

    SyncScrollMap m_scrollMap;
    QVector<Chunk> m_chunks;
    bool m_syncScrolling = false;
};

#endif // CONFLICT_MERGE_VIEWER_DOCK_H

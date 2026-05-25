#ifndef MARKDOWNPREVIEWOVERLAY_H
#define MARKDOWNPREVIEWOVERLAY_H

#include <QObject>
#include <QFutureWatcher>
#include <atomic>

#include "MarkdownRenderer.h"

class PreviewBrowser;
class ScintillaNext;
class NotepadNextApplication;

class MarkdownPreviewOverlay : public QObject
{
    Q_OBJECT

public:
    explicit MarkdownPreviewOverlay(ScintillaNext *editor, NotepadNextApplication *app);

    bool isActive() const { return m_active; }
    void activate();
    void deactivate();
    void toggle();
    void clearCachedContent();

signals:
    void activeChanged(bool active);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void renderAsync();
    void renderSync();
    void applyHtml(const QString &html);
    void scrollToEditorPosition();
    MarkdownRenderRequest buildRequest();
    void ensureBrowser();

    ScintillaNext *m_editor;
    NotepadNextApplication *m_app;
    PreviewBrowser *m_browser = nullptr;
    bool m_active = false;
    QString m_cachedHtml;
    std::atomic<int> m_renderGeneration{0};
    QFutureWatcher<MarkdownRenderResult> *m_watcher = nullptr;
};

#endif // MARKDOWNPREVIEWOVERLAY_H

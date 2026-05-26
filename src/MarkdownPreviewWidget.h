#ifndef MARKDOWNPREVIEWWIDGET_H
#define MARKDOWNPREVIEWWIDGET_H

#include "PreviewContentWidget.h"
#include "MarkdownRenderer.h"

#include <QTextBrowser>
#include <QFutureWatcher>
#include <QHash>
#include <QUrl>
#include <QTimer>
#include <atomic>

class NotepadNextApplication;

class MarkdownPreviewWidget : public PreviewContentWidget
{
    Q_OBJECT

public:
    explicit MarkdownPreviewWidget(NotepadNextApplication *app, QWidget *parent = nullptr);

    QString typeId() const override { return QStringLiteral("markdown"); }
    QString displayName() const override { return m_title; }
    void setContent(const QString &text, const QString &basePath) override;
    void refresh(const QString &text) override;
    void applyTheme(const QPalette &palette, bool isDark) override;

    void scrollToLine(int line);

private:
    void renderAsync(const QString &text);
    void applyHtml(const QString &html);
    MarkdownRenderRequest buildRequest(const QString &text);
    void onFontChanged();

    NotepadNextApplication *m_app;
    QTextBrowser *m_browser;
    QString m_title;
    QString m_basePath;
    QPalette m_palette;
    bool m_isDark = false;
    QString m_cachedHtml;
    QString m_lastSourceText;
    std::atomic<int> m_renderGeneration{0};
    QFutureWatcher<MarkdownRenderResult> *m_watcher = nullptr;

    QHash<QUrl, QVariant> m_imageCache;
    QTimer m_relayoutTimer;
};

#endif // MARKDOWNPREVIEWWIDGET_H

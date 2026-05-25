#include "MarkdownPreviewOverlay.h"
#include "PreviewBrowser.h"
#include "ScintillaNext.h"
#include "NotepadNextApplication.h"

#include <QEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QtConcurrent>

static constexpr int kSyncRenderThreshold = 100 * 1024; // 100KB

MarkdownPreviewOverlay::MarkdownPreviewOverlay(ScintillaNext *editor, NotepadNextApplication *app)
    : QObject(editor)
    , m_editor(editor)
    , m_app(app)
{
    m_editor->installEventFilter(this);
}

void MarkdownPreviewOverlay::ensureBrowser()
{
    if (m_browser)
        return;

    m_browser = new PreviewBrowser(m_editor);
    m_browser->hide();

    if (m_editor->isFile()) {
        QString dir = m_editor->getFileInfo().absolutePath();
        m_browser->setSearchPaths({dir});
    }

    connect(m_browser, &PreviewBrowser::dismissRequested, this, [this]() {
        deactivate();
    });

    connect(m_app, &NotepadNextApplication::effectiveThemeChanged, this, [this]() {
        if (m_active) {
            m_cachedHtml.clear();
            renderAsync();
        }
    });
}

void MarkdownPreviewOverlay::activate()
{
    if (m_active)
        return;

    ensureBrowser();
    m_active = true;
    m_browser->setGeometry(m_editor->rect());
    m_browser->show();
    m_browser->raise();
    m_browser->setFocus();

    if (!m_cachedHtml.isEmpty()) {
        m_browser->setHtml(m_cachedHtml);
        scrollToEditorPosition();
    } else if (m_editor->length() <= kSyncRenderThreshold) {
        renderSync();
    } else {
        m_browser->setHtml(QStringLiteral("<p><em>Rendering...</em></p>"));
        renderAsync();
    }

    emit activeChanged(true);
}

void MarkdownPreviewOverlay::deactivate()
{
    if (!m_active)
        return;

    m_active = false;
    if (m_browser)
        m_browser->hide();

    m_editor->grabFocus();
    emit activeChanged(false);
}

void MarkdownPreviewOverlay::toggle()
{
    if (m_active)
        deactivate();
    else
        activate();
}

void MarkdownPreviewOverlay::clearCachedContent()
{
    m_cachedHtml.clear();
    if (m_browser && !m_active)
        m_browser->clear();
}

MarkdownRenderRequest MarkdownPreviewOverlay::buildRequest()
{
    MarkdownRenderRequest req;
    req.sourceText = QString::fromUtf8(m_editor->getText(m_editor->length() + 1));
    req.palette = m_browser->palette();
    req.isDark = m_app->isEffectiveThemeDark();
    if (m_editor->isFile())
        req.basePath = m_editor->getFileInfo().absolutePath();

    QSet<QString> labels = MarkdownRenderer::scanFenceLabels(req.sourceText);
    for (const QString &label : labels) {
        QString normalized = MarkdownRenderer::normalizeFenceLabel(label);
        QString lexerName = m_app->resolveLexerName(normalized);
        if (lexerName.isEmpty()) {
            static const QHash<QString, QString> fallback = {
                {"cpp", "cpp"}, {"c", "cpp"}, {"python", "python"},
                {"javascript", "cpp"}, {"rust", "rust"}, {"java", "cpp"},
                {"go", "cpp"}, {"bash", "bash"}, {"html", "hypertext"},
                {"css", "css"}, {"json", "json"}, {"lua", "lua"},
            };
            lexerName = fallback.value(normalized);
        }
        req.resolvedLexers.insert(normalized, lexerName);
    }
    return req;
}

void MarkdownPreviewOverlay::renderSync()
{
    MarkdownRenderRequest req = buildRequest();
    MarkdownRenderResult result = MarkdownRenderer::render(req);
    applyHtml(result.html);
}

void MarkdownPreviewOverlay::renderAsync()
{
    int gen = m_renderGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    MarkdownRenderRequest req = buildRequest();

    if (m_watcher) {
        m_watcher->cancel();
        delete m_watcher;
    }

    m_watcher = new QFutureWatcher<MarkdownRenderResult>(this);
    connect(m_watcher, &QFutureWatcher<MarkdownRenderResult>::finished, this, [this, gen]() {
        if (m_renderGeneration.load(std::memory_order_relaxed) != gen) return;
        if (!m_active) return;
        MarkdownRenderResult result = m_watcher->result();
        applyHtml(result.html);
    });

    m_watcher->setFuture(QtConcurrent::run([req]() {
        return MarkdownRenderer::render(req);
    }));
}

void MarkdownPreviewOverlay::applyHtml(const QString &html)
{
    m_cachedHtml = html;
    if (m_browser && m_active) {
        m_browser->setHtml(html);
        scrollToEditorPosition();
    }
}

void MarkdownPreviewOverlay::scrollToEditorPosition()
{
    if (!m_browser) return;
    int firstLine = int(m_editor->firstVisibleLine()) + 1;
    int anchorLine = (firstLine / 10) * 10;
    if (anchorLine < 1) anchorLine = 1;
    m_browser->scrollToAnchor(QStringLiteral("L%1").arg(anchorLine));
}

bool MarkdownPreviewOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editor && event->type() == QEvent::Resize) {
        if (m_browser && m_active)
            m_browser->setGeometry(m_editor->rect());
    }
    return QObject::eventFilter(watched, event);
}

#include "MarkdownPreviewWidget.h"
#include "NotepadNextApplication.h"
#include "ApplicationSettings.h"

#include <QVBoxLayout>
#include <QImage>
#include <QScrollBar>
#include <QTextDocument>
#include <QtConcurrent>
#include <QKeyEvent>

MarkdownPreviewWidget::MarkdownPreviewWidget(NotepadNextApplication *app, QWidget *parent)
    : PreviewContentWidget(parent)
    , m_app(app)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_browser = new QTextBrowser(this);
    m_browser->setOpenExternalLinks(true);
    m_browser->setFrameShape(QFrame::NoFrame);
    m_browser->setReadOnly(true);
    layout->addWidget(m_browser);

    m_relayoutTimer.setSingleShot(true);
    m_relayoutTimer.setInterval(100);
    connect(&m_relayoutTimer, &QTimer::timeout, this, [this]() {
        m_browser->document()->markContentsDirty(0, m_browser->document()->characterCount());
    });

    m_palette = app->palette();
    m_isDark = app->isEffectiveThemeDark();

    auto *settings = app->getSettings();
    connect(settings, &ApplicationSettings::fontNameChanged, this, &MarkdownPreviewWidget::onFontChanged);
    connect(settings, &ApplicationSettings::fontSizeChanged, this, &MarkdownPreviewWidget::onFontChanged);
}

void MarkdownPreviewWidget::setContent(const QString &text, const QString &basePath)
{
    m_basePath = basePath;
    if (!basePath.isEmpty())
        m_browser->setSearchPaths({basePath});
    renderAsync(text);
}

void MarkdownPreviewWidget::refresh(const QString &text)
{
    renderAsync(text);
}

void MarkdownPreviewWidget::applyTheme(const QPalette &palette, bool isDark)
{
    m_palette = palette;
    m_isDark = isDark;
    m_browser->setPalette(palette);
    if (!m_cachedHtml.isEmpty()) {
        m_cachedHtml.clear();
    }
}

void MarkdownPreviewWidget::scrollToLine(int line)
{
    int anchorLine = (line / 10) * 10;
    if (anchorLine < 1) anchorLine = 1;
    m_browser->scrollToAnchor(QStringLiteral("L%1").arg(anchorLine));
}

MarkdownRenderRequest MarkdownPreviewWidget::buildRequest(const QString &text)
{
    MarkdownRenderRequest req;
    req.sourceText = text;
    req.palette = m_palette;
    req.isDark = m_isDark;
    req.basePath = m_basePath;

    auto *settings = m_app->getSettings();
    req.fontFamily = settings->fontName();
    req.fontSize = settings->fontSize();

    QSet<QString> labels = MarkdownRenderer::scanFenceLabels(text);
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

void MarkdownPreviewWidget::renderAsync(const QString &text)
{
    m_lastSourceText = text;
    int gen = m_renderGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    MarkdownRenderRequest req = buildRequest(text);

    if (m_watcher) {
        m_watcher->cancel();
        delete m_watcher;
    }

    m_watcher = new QFutureWatcher<MarkdownRenderResult>(this);
    connect(m_watcher, &QFutureWatcher<MarkdownRenderResult>::finished, this, [this, gen]() {
        if (m_renderGeneration.load(std::memory_order_relaxed) != gen) return;
        MarkdownRenderResult result = m_watcher->result();
        applyHtml(result.html);
    });

    m_watcher->setFuture(QtConcurrent::run([req]() {
        return MarkdownRenderer::render(req);
    }));
}

void MarkdownPreviewWidget::applyHtml(const QString &html)
{
    m_cachedHtml = html;
    int scrollPos = m_browser->verticalScrollBar()->value();
    m_browser->setHtml(html);
    m_browser->verticalScrollBar()->setValue(scrollPos);
}

void MarkdownPreviewWidget::onFontChanged()
{
    if (m_cachedHtml.isEmpty()) return;
    renderAsync(m_lastSourceText);
}

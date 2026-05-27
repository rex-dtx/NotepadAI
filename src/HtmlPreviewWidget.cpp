#include "HtmlPreviewWidget.h"
#include "NotepadNextApplication.h"
#include "FileEncodingDetector.h"

#include <QVBoxLayout>
#include <QToolButton>
#include <QStyle>
#include <QFileInfo>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include <QEvent>
#include <QScrollBar>

HtmlPreviewWidget::HtmlPreviewWidget(NotepadNextApplication *app, QWidget *parent)
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

    // Floating buttons on the viewport (top-right, horizontal row)
    m_refreshBtn = new QToolButton(m_browser->viewport());
    m_refreshBtn->setAutoRaise(true);
    m_refreshBtn->setIconSize(QSize(16, 16));
    m_refreshBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_refreshBtn->setToolTip(tr("Refresh"));
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QToolButton::clicked, this, &HtmlPreviewWidget::doRefreshFromDisk);

    m_openBtn = new QToolButton(m_browser->viewport());
    m_openBtn->setAutoRaise(true);
    m_openBtn->setIconSize(QSize(16, 16));
    m_openBtn->setIcon(style()->standardIcon(QStyle::SP_CommandLink));
    m_openBtn->setToolTip(tr("Open in Browser"));
    m_openBtn->setCursor(Qt::PointingHandCursor);
    connect(m_openBtn, &QToolButton::clicked, this, &HtmlPreviewWidget::doOpenInBrowser);

    m_browser->viewport()->installEventFilter(this);
    repositionButtons();
}

void HtmlPreviewWidget::setContent(const QString &text, const QString &basePath)
{
    if (!basePath.isEmpty())
        m_browser->setSearchPaths({basePath});
    m_browser->setHtml(text);
}

void HtmlPreviewWidget::refresh(const QString &text)
{
    int scrollPos = m_browser->verticalScrollBar()->value();
    m_browser->setHtml(text);
    m_browser->verticalScrollBar()->setValue(scrollPos);
}

void HtmlPreviewWidget::applyTheme(const QPalette &palette, bool isDark)
{
    Q_UNUSED(isDark)
    m_browser->setPalette(palette);
}

void HtmlPreviewWidget::setFilePath(const QString &filePath)
{
    m_filePath = filePath;
    m_displayName = QFileInfo(filePath).fileName();
}

bool HtmlPreviewWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_browser->viewport() && event->type() == QEvent::Resize) {
        repositionButtons();
    }
    return PreviewContentWidget::eventFilter(watched, event);
}

void HtmlPreviewWidget::repositionButtons()
{
    const int margin = 6;
    const int spacing = 4;
    QSize refreshSize = m_refreshBtn->sizeHint();
    QSize openSize = m_openBtn->sizeHint();
    int vpWidth = m_browser->viewport()->width();

    int x = vpWidth - margin - openSize.width();
    m_openBtn->move(x, margin);
    m_openBtn->raise();

    x -= spacing + refreshSize.width();
    m_refreshBtn->move(x, margin);
    m_refreshBtn->raise();
}

void HtmlPreviewWidget::doRefreshFromDisk()
{
    if (m_filePath.isEmpty()) return;

    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) return;

    QByteArray raw = f.readAll();
    f.close();

    QString text;
    FileEncodingDetector::decode(raw, text);

    int scrollPos = m_browser->verticalScrollBar()->value();
    m_browser->setHtml(text);
    m_browser->verticalScrollBar()->setValue(scrollPos);
}

void HtmlPreviewWidget::doOpenInBrowser()
{
    if (!m_filePath.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_filePath));
}

#include "PreviewBrowser.h"

#include <QImage>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextDocument>
#include <QtConcurrent>

PreviewBrowser::PreviewBrowser(QWidget *parent)
    : QTextBrowser(parent)
{
    setOpenExternalLinks(true);
    setFrameShape(QFrame::NoFrame);
    setReadOnly(true);

    m_relayoutTimer.setSingleShot(true);
    m_relayoutTimer.setInterval(100);
    connect(&m_relayoutTimer, &QTimer::timeout, this, [this]() {
        document()->markContentsDirty(0, document()->characterCount());
        m_pendingImages = 0;
    });
}

QVariant PreviewBrowser::loadResource(int type, const QUrl &name)
{
    if (type != QTextDocument::ImageResource)
        return QTextBrowser::loadResource(type, name);

    auto cached = m_imageCache.constFind(name);
    if (cached != m_imageCache.constEnd())
        return cached.value();

    if (!name.isLocalFile())
        return QVariant();

    QImage placeholder(1, 1, QImage::Format_ARGB32);
    placeholder.fill(Qt::transparent);
    document()->addResource(QTextDocument::ImageResource, name, placeholder);

    m_pendingImages++;
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, name]() {
        QImage img = watcher->result();
        if (!img.isNull()) {
            m_imageCache.insert(name, img);
            document()->addResource(QTextDocument::ImageResource, name, img);
            if (!m_relayoutTimer.isActive())
                m_relayoutTimer.start();
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([name]() -> QImage {
        QImage img;
        if (name.isLocalFile())
            img.load(name.toLocalFile());
        if (img.width() > 2048 || img.height() > 2048)
            img = img.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return img;
    }));

    return placeholder;
}

void PreviewBrowser::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit dismissRequested();
        return;
    }

    if (event->matches(QKeySequence::Copy) || event->matches(QKeySequence::SelectAll)) {
        QTextBrowser::keyPressEvent(event);
        return;
    }

    if (!event->text().isEmpty() && event->modifiers() == Qt::NoModifier) {
        emit editKeyPressed(event);
        emit dismissRequested();
        return;
    }

    QTextBrowser::keyPressEvent(event);
}

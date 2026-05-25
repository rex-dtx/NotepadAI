#ifndef PREVIEWBROWSER_H
#define PREVIEWBROWSER_H

#include <QTextBrowser>
#include <QHash>
#include <QUrl>
#include <QTimer>

class PreviewBrowser : public QTextBrowser
{
    Q_OBJECT

public:
    explicit PreviewBrowser(QWidget *parent = nullptr);

protected:
    QVariant loadResource(int type, const QUrl &name) override;
    void keyPressEvent(QKeyEvent *event) override;

signals:
    void dismissRequested();
    void editKeyPressed(QKeyEvent *event);

private:
    void onImageLoaded(const QUrl &url, const QImage &image);

    QHash<QUrl, QVariant> m_imageCache;
    QTimer m_relayoutTimer;
    int m_pendingImages = 0;
};

#endif // PREVIEWBROWSER_H

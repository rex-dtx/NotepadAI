#ifndef HTMLPREVIEWWIDGET_H
#define HTMLPREVIEWWIDGET_H

#include "PreviewContentWidget.h"

#include <QTextBrowser>

class NotepadNextApplication;
class QToolButton;

class HtmlPreviewWidget : public PreviewContentWidget
{
    Q_OBJECT

public:
    explicit HtmlPreviewWidget(NotepadNextApplication *app, QWidget *parent = nullptr);

    QString typeId() const override { return QStringLiteral("html"); }
    QString displayName() const override { return m_displayName; }
    void setContent(const QString &text, const QString &basePath) override;
    void refresh(const QString &text) override;
    void applyTheme(const QPalette &palette, bool isDark) override;

    void setFilePath(const QString &filePath);
    QString filePath() const { return m_filePath; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void repositionButtons();
    void doRefreshFromDisk();
    void doOpenInBrowser();

    NotepadNextApplication *m_app;
    QTextBrowser *m_browser;
    QToolButton *m_refreshBtn = nullptr;
    QToolButton *m_openBtn = nullptr;
    QString m_filePath;
    QString m_displayName;
};

#endif // HTMLPREVIEWWIDGET_H

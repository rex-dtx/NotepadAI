#ifndef PREVIEWCONTENTWIDGET_H
#define PREVIEWCONTENTWIDGET_H

#include <QWidget>
#include <QPalette>

class PreviewContentWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewContentWidget(QWidget *parent = nullptr) : QWidget(parent) {}

    virtual QString typeId() const = 0;
    virtual QString displayName() const = 0;
    virtual void setContent(const QString &text, const QString &basePath) = 0;
    virtual void refresh(const QString &text) = 0;
    virtual void applyTheme(const QPalette &palette, bool isDark) = 0;

signals:
    void titleChanged(const QString &title);
};

#endif // PREVIEWCONTENTWIDGET_H

#ifndef MARKDOWNRENDERER_H
#define MARKDOWNRENDERER_H

#include <QHash>
#include <QPalette>
#include <QString>

struct MarkdownRenderRequest {
    QString sourceText;
    QHash<QString, QString> resolvedLexers;
    QPalette palette;
    bool isDark = false;
    QString basePath;
};

struct MarkdownRenderResult {
    QString html;
    bool truncated = false;
};

class MarkdownRenderer
{
public:
    static MarkdownRenderResult render(const MarkdownRenderRequest &request);

    static QSet<QString> scanFenceLabels(const QString &source);
    static QString normalizeFenceLabel(const QString &label);

    static constexpr size_t kMaxHtmlBytes = 512 * 1024;

private:
    static QString buildStyleBlock(const QPalette &palette, bool isDark);
    static QString highlightCodeBlock(const QByteArray &code, const QString &lexerName, bool isDark);
};

#endif // MARKDOWNRENDERER_H

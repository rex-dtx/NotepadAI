#include <QtTest>
#include "MarkdownRenderer.h"

class TestMarkdownRenderer : public QObject
{
    Q_OBJECT

private:
    MarkdownRenderRequest makeRequest(const QString &source, bool isDark = false) {
        MarkdownRenderRequest req;
        req.sourceText = source;
        req.palette = QPalette();
        req.isDark = isDark;
        return req;
    }

private slots:
    void render_heading_emitsH1()
    {
        auto result = MarkdownRenderer::render(makeRequest("# Hello"));
        QVERIFY(result.html.contains("<h1>"));
        QVERIFY(result.html.contains("Hello"));
    }

    void render_paragraph_wrapsInP()
    {
        auto result = MarkdownRenderer::render(makeRequest("Some text"));
        QVERIFY(result.html.contains("<p>"));
        QVERIFY(result.html.contains("Some text"));
    }

    void render_codeFence_emitsPreCode()
    {
        auto result = MarkdownRenderer::render(makeRequest("```\ncode\n```"));
        QVERIFY(result.html.contains("<pre>"));
        QVERIFY(result.html.contains("<code>"));
        QVERIFY(result.html.contains("code"));
    }

    void render_table_emitsTableTags()
    {
        QString md = "| A | B |\n|---|---|\n| 1 | 2 |";
        auto result = MarkdownRenderer::render(makeRequest(md));
        QVERIFY(result.html.contains("<table>"));
        QVERIFY(result.html.contains("<td>"));
    }

    void render_link_emitsAnchor()
    {
        auto result = MarkdownRenderer::render(makeRequest("[link](http://example.com)"));
        QVERIFY(result.html.contains("<a"));
        QVERIFY(result.html.contains("http://example.com"));
    }

    void render_htmlEscaping_lessThan()
    {
        auto result = MarkdownRenderer::render(makeRequest("a < b"));
        QVERIFY(result.html.contains("&lt;"));
    }

    void render_smallFile_notTruncated()
    {
        auto result = MarkdownRenderer::render(makeRequest("Hello world"));
        QVERIFY(!result.truncated);
        QVERIFY(!result.html.contains("[Preview truncated"));
    }

    void render_largeFile_truncated()
    {
        QString large;
        large.reserve(600000);
        for (int i = 0; i < 30000; ++i)
            large += "This is a line of text that is fairly long to generate output.\n";
        auto result = MarkdownRenderer::render(makeRequest(large));
        QVERIFY(result.truncated);
        QVERIFY(result.html.contains("[Preview truncated"));
    }

    void scanFenceLabels_basic()
    {
        QString md = "```cpp\nint x;\n```\n\n```python\npass\n```";
        QSet<QString> labels = MarkdownRenderer::scanFenceLabels(md);
        QVERIFY(labels.contains("cpp"));
        QVERIFY(labels.contains("python"));
    }

    void scanFenceLabels_empty()
    {
        QSet<QString> labels = MarkdownRenderer::scanFenceLabels("```\ncode\n```");
        QVERIFY(labels.isEmpty());
    }

    void normalizeFence_alias()
    {
        QCOMPARE(MarkdownRenderer::normalizeFenceLabel("c++"), QString("cpp"));
        QCOMPARE(MarkdownRenderer::normalizeFenceLabel("js"), QString("javascript"));
        QCOMPARE(MarkdownRenderer::normalizeFenceLabel("ts"), QString("javascript"));
        QCOMPARE(MarkdownRenderer::normalizeFenceLabel("py"), QString("python"));
        QCOMPARE(MarkdownRenderer::normalizeFenceLabel("sh"), QString("bash"));
    }

    void normalizeFence_passthrough()
    {
        QCOMPARE(MarkdownRenderer::normalizeFenceLabel("rust"), QString("rust"));
        QCOMPARE(MarkdownRenderer::normalizeFenceLabel("lua"), QString("lua"));
    }

    void normalizeFence_caseInsensitive()
    {
        QCOMPARE(MarkdownRenderer::normalizeFenceLabel("CPP"), QString("cpp"));
        QCOMPARE(MarkdownRenderer::normalizeFenceLabel("Python"), QString("python"));
    }

    void normalizeFence_empty()
    {
        QCOMPARE(MarkdownRenderer::normalizeFenceLabel(""), QString(""));
    }

    void render_containsStyleBlock()
    {
        auto result = MarkdownRenderer::render(makeRequest("# Test"));
        QVERIFY(result.html.contains("<style>"));
        QVERIFY(result.html.contains("</style>"));
    }

    void render_containsAnchors()
    {
        QString md;
        for (int i = 0; i < 30; ++i)
            md += "Line " + QString::number(i) + "\n\n";
        auto result = MarkdownRenderer::render(makeRequest(md));
        QVERIFY(result.html.contains("id=\"L"));
    }

    void render_darkMode_differentStyleBlock()
    {
        auto light = MarkdownRenderer::render(makeRequest("# Test", false));
        auto dark = MarkdownRenderer::render(makeRequest("# Test", true));
        QVERIFY(light.html != dark.html);
    }

    void highlightedCodeBlock_containsSpans()
    {
        MarkdownRenderRequest req = makeRequest("```cpp\nint main() { return 0; }\n```");
        req.resolvedLexers.insert("cpp", "cpp");
        auto result = MarkdownRenderer::render(req);
        QVERIFY(result.html.contains("<span style=\"color:"));
    }

    void highlightedCodeBlock_unknownLanguage_noSpans()
    {
        auto result = MarkdownRenderer::render(makeRequest("```unknownlang\nfoo\n```"));
        QVERIFY(result.html.contains("foo"));
    }
};

QTEST_MAIN(TestMarkdownRenderer)
#include "test_markdown_renderer.moc"

#include "PreviewTabManager.h"
#include "DockedEditor.h"
#include "ScintillaNext.h"
#include "NotepadNextApplication.h"
#include "MarkdownRenderer.h"
#include "MarkdownPreviewWidget.h"

#include "DockWidget.h"
#include "DockWidgetTab.h"
#include "DockAreaWidget.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QPainter>
#include <QtConcurrent>

#include <ScintillaTypes.h>

static QIcon tintIcon(const QString &svgPath, const QColor &color)
{
    QIcon source(svgPath);
    if (source.isNull()) return source;
    QIcon dst;
    for (const QSize &sz : {QSize(16, 16), QSize(24, 24), QSize(32, 32)}) {
        QPixmap pm = source.pixmap(sz);
        QPainter p(&pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), color);
        p.end();
        dst.addPixmap(pm);
    }
    return dst;
}

PreviewTabManager::PreviewTabManager(NotepadNextApplication *app, DockedEditor *dockedEditor, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_dockedEditor(dockedEditor)
{
    connect(app, &NotepadNextApplication::effectiveThemeChanged, this, [this]() {
        QPalette pal = m_app->palette();
        bool isDark = m_app->isEffectiveThemeDark();
        QColor iconColor = pal.color(QPalette::ButtonText);
        for (auto &entry : m_previews) {
            if (entry.widget)
                entry.widget->applyTheme(pal, isDark);
            if (entry.dockWidget && !entry.iconPath.isEmpty())
                entry.dockWidget->tabWidget()->setIcon(tintIcon(entry.iconPath, iconColor));
        }
    });
}

void PreviewTabManager::registerType(const QString &typeId, const TypeRegistration &reg)
{
    m_registry.insert(typeId, reg);
}

const PreviewTabManager::TypeRegistration *PreviewTabManager::findRegistration(const QString &filePath) const
{
    QString ext = QFileInfo(filePath).suffix().toLower();
    for (auto it = m_registry.constBegin(); it != m_registry.constEnd(); ++it) {
        if (it.value().extensions.contains(ext))
            return &it.value();
    }
    return nullptr;
}

bool PreviewTabManager::canPreview(const QString &filePath) const
{
    return findRegistration(filePath) != nullptr;
}

int PreviewTabManager::debounceMs(int docLength) const
{
    if (docLength < 10 * 1024)  return 150;
    if (docLength < 100 * 1024) return 300;
    return 800;
}

void PreviewTabManager::openOrFocusPreview(ScintillaNext *sourceEditor)
{
    if (!sourceEditor) return;

    auto it = m_previews.find(sourceEditor);
    if (it != m_previews.end() && it->widget) {
        if (it->dockWidget)
            it->dockWidget->raise();
        it->widget->setFocus();
        return;
    }

    QString filePath = sourceEditor->isFile() ? sourceEditor->getFilePath() : QString();
    const TypeRegistration *reg = nullptr;
    if (!filePath.isEmpty())
        reg = findRegistration(filePath);
    if (!reg) {
        for (auto rit = m_registry.constBegin(); rit != m_registry.constEnd(); ++rit) {
            if (rit.value().extensions.contains(QStringLiteral("md"))) {
                reg = &rit.value();
                break;
            }
        }
    }
    if (!reg) return;

    PreviewContentWidget *preview = reg->factory(nullptr);
    if (!preview) return;

    preview->applyTheme(m_app->palette(), m_app->isEffectiveThemeDark());

    QString basePath;
    if (sourceEditor->isFile())
        basePath = QFileInfo(sourceEditor->getFilePath()).absolutePath();

    QString text = QString::fromUtf8(sourceEditor->getText(sourceEditor->length() + 1));
    preview->setContent(text, basePath);

    QString title = sourceEditor->isFile()
        ? QFileInfo(sourceEditor->getFilePath()).fileName()
        : sourceEditor->getName();

    ads::CDockWidget *dockWidget = m_dockedEditor->addPreviewTab(
        preview, title, tintIcon(reg->iconPath, m_app->palette().color(QPalette::ButtonText)));

    PreviewEntry entry;
    entry.widget = preview;
    entry.dockWidget = dockWidget;
    entry.iconPath = reg->iconPath;
    entry.debounceTimer = new QTimer(this);
    entry.debounceTimer->setSingleShot(true);
    m_previews.insert(sourceEditor, entry);

    connect(entry.debounceTimer, &QTimer::timeout, this, [this, sourceEditor]() {
        performRender(sourceEditor);
    });

    // Live update on content change
    connect(sourceEditor, &ScintillaNext::updateUi, this, [this, sourceEditor](Scintilla::Update updated) {
        if (!m_previews.contains(sourceEditor)) return;
        if (Scintilla::FlagSet(updated, Scintilla::Update::Content))
            scheduleRender(sourceEditor);
        else if (Scintilla::FlagSet(updated, Scintilla::Update::VScroll))
            syncScroll(sourceEditor);
    });

    // Lifecycle: close preview when source editor is destroyed
    connect(sourceEditor, &QObject::destroyed, this, [this, sourceEditor]() {
        auto it = m_previews.find(sourceEditor);
        if (it != m_previews.end()) {
            if (it->dockWidget)
                it->dockWidget->closeDockWidget();
            delete it->debounceTimer;
            m_previews.erase(it);
        }
    });

    // Cleanup hash when preview widget is destroyed (e.g. user closes tab)
    connect(preview, &QObject::destroyed, this, [this, sourceEditor]() {
        auto it = m_previews.find(sourceEditor);
        if (it != m_previews.end()) {
            delete it->debounceTimer;
            m_previews.erase(it);
        }
    });

    // Update title on rename
    connect(sourceEditor, &ScintillaNext::renamed, this, [this, sourceEditor]() {
        auto it = m_previews.find(sourceEditor);
        if (it == m_previews.end() || !it->widget || !it->dockWidget) return;
        QString newTitle = sourceEditor->isFile()
            ? QFileInfo(sourceEditor->getFilePath()).fileName()
            : sourceEditor->getName();
        it->dockWidget->setWindowTitle(newTitle);
        QString text = QString::fromUtf8(sourceEditor->getText(sourceEditor->length() + 1));
        QString basePath;
        if (sourceEditor->isFile())
            basePath = QFileInfo(sourceEditor->getFilePath()).absolutePath();
        it->widget->setContent(text, basePath);
    });

    emit previewOpened(preview);
}

void PreviewTabManager::closePreview(ScintillaNext *sourceEditor)
{
    auto it = m_previews.find(sourceEditor);
    if (it == m_previews.end()) return;
    if (it->dockWidget)
        it->dockWidget->closeDockWidget();
}

PreviewContentWidget *PreviewTabManager::previewForEditor(ScintillaNext *sourceEditor) const
{
    auto it = m_previews.constFind(sourceEditor);
    if (it != m_previews.constEnd())
        return it->widget.data();
    return nullptr;
}

void PreviewTabManager::scheduleRender(ScintillaNext *sourceEditor)
{
    auto it = m_previews.find(sourceEditor);
    if (it == m_previews.end()) return;
    int ms = debounceMs(static_cast<int>(sourceEditor->length()));
    it->debounceTimer->start(ms);
}

void PreviewTabManager::performRender(ScintillaNext *sourceEditor)
{
    auto it = m_previews.find(sourceEditor);
    if (it == m_previews.end() || !it->widget) return;

    PreviewContentWidget *widget = it->widget;
    QString text = QString::fromUtf8(sourceEditor->getText(sourceEditor->length() + 1));
    QString basePath;
    if (sourceEditor->isFile())
        basePath = QFileInfo(sourceEditor->getFilePath()).absolutePath();

    widget->refresh(text);
}

void PreviewTabManager::syncScroll(ScintillaNext *sourceEditor)
{
    auto it = m_previews.find(sourceEditor);
    if (it == m_previews.end() || !it->widget) return;

    auto *mdWidget = qobject_cast<MarkdownPreviewWidget *>(it->widget.data());
    if (mdWidget) {
        int firstLine = static_cast<int>(sourceEditor->firstVisibleLine()) + 1;
        mdWidget->scrollToLine(firstLine);
    }
}

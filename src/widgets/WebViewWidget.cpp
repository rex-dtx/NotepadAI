/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "WebViewWidget.h"

#include <QFontMetrics>
#include <QStyle>

WebViewWidget::WebViewWidget(const QString &appId, const QUrl &url, QWidget *parent)
    : QWidget(parent)
    , m_appId(appId)
    , m_url(url)
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    setupToolbar();
}

void WebViewWidget::setupToolbar()
{
    auto *toolbarWidget = new QWidget(this);
    toolbarWidget->setFixedHeight(24);
    m_toolbarLayout = new QHBoxLayout(toolbarWidget);
    m_toolbarLayout->setContentsMargins(4, 0, 4, 0);
    m_toolbarLayout->setSpacing(4);

    m_reloadBtn = new QToolButton(toolbarWidget);
    m_reloadBtn->setAutoRaise(true);
    m_reloadBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_reloadBtn->setToolTip(tr("Reload"));
    m_reloadBtn->setIconSize(QSize(14, 14));
    connect(m_reloadBtn, &QToolButton::clicked, this, &WebViewWidget::reload);
    m_toolbarLayout->addWidget(m_reloadBtn);

    m_urlLabel = new QLabel(toolbarWidget);
    m_urlLabel->setText(m_url.toString());
    m_urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont labelFont = m_urlLabel->font();
    labelFont.setPointSize(labelFont.pointSize() - 1);
    m_urlLabel->setFont(labelFont);
    m_urlLabel->setWordWrap(false);
    m_urlLabel->setMinimumWidth(0);
    m_urlLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbarLayout->addWidget(m_urlLabel, 1);

    m_stopBtn = new QToolButton(toolbarWidget);
    m_stopBtn->setAutoRaise(true);
    m_stopBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserStop));
    m_stopBtn->setToolTip(tr("Stop"));
    m_stopBtn->setIconSize(QSize(14, 14));
    m_stopBtn->hide(); // Hidden by default, shown during loading
    connect(m_stopBtn, &QToolButton::clicked, this, &WebViewWidget::stop);
    m_toolbarLayout->addWidget(m_stopBtn);

    m_mainLayout->addWidget(toolbarWidget);
}

void WebViewWidget::setLoading(bool loading)
{
    if (m_stopBtn)
        m_stopBtn->setVisible(loading);
    emit loadingStateChanged(loading);
}

// Factory implementation — returns nullptr on unsupported platforms.
// Platform-specific create() is defined in WebViewWidget_win.cpp / _mac.mm.
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
WebViewWidget *WebViewWidget::create(const QString & /*appId*/, const QUrl & /*url*/, QWidget * /*parent*/)
{
    return nullptr; // Linux: no embedded webview, use xdg-open
}
#endif

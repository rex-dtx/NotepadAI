/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

// Abstract webview widget. Platform implementations live in
// WebViewWidget_win.cpp (WebView2) and WebViewWidget_mac.mm (WKWebView).
// On Linux, no webview is embedded — xdg-open is used instead.
class WebViewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WebViewWidget(const QString &appId, const QUrl &url, QWidget *parent = nullptr);
    ~WebViewWidget() override = default;

    virtual void initialize() = 0;
    virtual void navigate(const QUrl &url) = 0;
    virtual void reload() = 0;
    virtual void stop() = 0;
    virtual void destroy() = 0;
    virtual QString debugInfo() const { return QString(); }

    // Factory: creates the platform-appropriate subclass.
    // Returns nullptr on Linux (caller should use xdg-open fallback).
    static WebViewWidget *create(const QString &appId, const QUrl &url, QWidget *parent = nullptr);

signals:
    void navigationCompleted(bool success, const QString &error);
    void processFailed(const QString &description);
    void loadingStateChanged(bool loading);

protected:
    // Subclasses add their native view below the toolbar.
    QVBoxLayout *mainLayout() const { return m_mainLayout; }
    QString appId() const { return m_appId; }
    QUrl initialUrl() const { return m_url; }

    void setLoading(bool loading);

private:
    void setupToolbar();

    QString m_appId;
    QUrl m_url;
    QVBoxLayout *m_mainLayout = nullptr;
    QHBoxLayout *m_toolbarLayout = nullptr;
    QToolButton *m_reloadBtn = nullptr;
    QLabel *m_urlLabel = nullptr;
    QToolButton *m_stopBtn = nullptr;
};
